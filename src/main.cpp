#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_MAX1704X.h>
#include <ArduinoJson.h>
#include "SparkFun_Bio_Sensor_Hub_Library.h"

// ===================== الإعدادات =====================
const char *WIFI_SSID = "nadeen";
const char *WIFI_PASSWORD = "12345689";
const char *FIREBASE_URL = "https://rakib-testing-default-rtdb.asia-southeast1.firebasedatabase.app/testData";

// الأقطاب (Pins)
#define SDA_PIN 21
#define SCL_PIN 22
#define BIO_RST_PIN 25
#define BIO_MFIO_PIN 33
// الكائنات
Adafruit_MPU6050 mpu;
Adafruit_MAX17048 maxlipo;
SparkFun_Bio_Sensor_Hub bioHub(BIO_RST_PIN, BIO_MFIO_PIN);

// متغيرات عالمية (Global)
volatile int16_t g_ax, g_ay, g_az;
volatile int g_hr, g_spo2, g_conf, g_status;
volatile uint32_t g_ir, g_red;
volatile float g_batt_v;
volatile int g_batt_pct;

bool loggingActive = false;
String currentTestName = "test_1";
unsigned long uploadInterval = 2000;
bool batteryFound = false;

TaskHandle_t SensorTaskHandle;
TaskHandle_t UploadTaskHandle;

// ===================== وظيفة قراءة الحساسات والبطارية (Core 0) =====================
void SensorTask(void *pvParameters)
{
  Serial.print("Sensor Task running on Core: ");
  Serial.println(xPortGetCoreID());

  for (;;)
  {
    // 1. قراءة IMU
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    g_ax = (int16_t)(a.acceleration.x * 1000);
    g_ay = (int16_t)(a.acceleration.y * 1000);
    g_az = (int16_t)(a.acceleration.z * 1000);

    // 2. قراءة Bio Sensor
    bioData body = bioHub.readSensorBpm();
    g_hr = body.heartRate;
    g_spo2 = body.oxygen;
    g_conf = body.confidence;
    g_status = body.status;
    g_ir = body.irLed;
    g_red = body.redLed;

    // 3. قراءة البطارية (MAX17048 Fuel Gauge)
    if (batteryFound) {
      g_batt_v   = maxlipo.cellVoltage();
      g_batt_pct = (int)maxlipo.cellPercent();
    }

    delay(20); // تردد قراءة 50Hz كافٍ جداً للحساسات
  }
}

// ===================== وظيفة الرفع للإنترنت (Core 1) =====================
void UploadTask(void *pvParameters)
{
  for (;;)
  {
    if (loggingActive && WiFi.status() == WL_CONNECTED)
    {
      HTTPClient http;
      String url = String(FIREBASE_URL) + "/" + currentTestName + "/log.json";

      http.begin(url);
      http.addHeader("Content-Type", "application/json");
      http.setTimeout(1500);

      JsonDocument doc;
      doc["time"] = millis();

      JsonObject imu = doc["imu"].to<JsonObject>();
      imu["ax"] = g_ax;
      imu["ay"] = g_ay;
      imu["az"] = g_az;

      JsonObject ppg = doc["ppg"].to<JsonObject>();
      ppg["hr"] = g_hr;
      ppg["spo2"] = g_spo2;
      ppg["conf"] = g_conf;
      ppg["ir"] = g_ir;
      ppg["red"] = g_red;
      ppg["status"] = g_status;

      JsonObject pwr = doc["power"].to<JsonObject>();
      pwr["v"] = g_batt_v;
      pwr["pct"] = g_batt_pct;

      String json;
      serializeJson(doc, json);

      int httpCode = http.POST(json);
      http.end();

      // --- التعديل هنا لإظهار البيانات في الـ Console ---
      Serial.print("[IMU] ");
      Serial.print("AX=");
      Serial.print(g_ax);
      Serial.print(" AY=");
      Serial.print(g_ay);
      Serial.print(" AZ=");
      Serial.print(g_az);
      Serial.print(" | [PPG] ");
      Serial.print("IR=");
      Serial.print(g_ir);
      Serial.print(" RED=");
      Serial.print(g_red);
      Serial.print(" HR=");
      Serial.print(g_hr);
      Serial.print(" SPO2=");
      Serial.print(g_spo2);
      Serial.print(" CONF=");
      Serial.print(g_conf);
      Serial.print(" STATUS=");
      Serial.print(g_status);
      Serial.print(" VALID=");
      Serial.print(g_conf > 90 ? "1" : "0");
      Serial.print(" | [BATT] ");
      Serial.print(g_batt_pct);
      Serial.print("% (");
      Serial.print(g_batt_v);
      Serial.print("V)");
      Serial.print(" -> ");
      Serial.println(httpCode);
    }

    delay(uploadInterval);
  }
}

// ===================== الإعدادات (Setup) =====================
void setup()
{
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  // 1. تهيئة الحساسات
  if (!mpu.begin())
  {
    Serial.println("Failed to find MPU6050 chip");
    while (1)
    {
      delay(10);
    }
  }

  // تهيئة MAX17048 قبل Bio Hub — لأن bioHub.begin() يعيد تشغيل I2C عبر RST pin
  batteryFound = maxlipo.begin();
  if (!batteryFound) {
    Serial.println("WARNING: MAX17048 not found at 0x36");
  } else {
    maxlipo.quickStart();  // recalibrate SOC from OCV on startup
    Serial.println("✅ MAX17048 Fuel Gauge Ready");
  }

  if (bioHub.begin() == 0)
  {
    bioHub.configSensorBpm(MODE_TWO);
    bioHub.setPulseWidth(411);
    bioHub.setAdcRange(4096);
    Serial.println("✅ Sensors Initialized");
  }

  // 2. الاتصال بالـ WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi Connected");

  // 3. إنشاء المهام وتوزيعها على الأنوية
  // المهمة الأولى: قراءة الحساسات (أولوية عالية على النواة 0)
  xTaskCreatePinnedToCore(SensorTask, "SensorTask", 4096, NULL, 3, &SensorTaskHandle, 0);

  // المهمة الثانية: الرفع للإنترنت (أولوية منخفضة على النواة 1)
  xTaskCreatePinnedToCore(UploadTask, "UploadTask", 8192, NULL, 1, &UploadTaskHandle, 1);

  Serial.println("READY. Type START or STOP");
}

void loop()
{
  // الـ loop سيبقى فقط لاستلام الأوامر من الـ Serial
  if (Serial.available())
  {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "START")
    {
      loggingActive = true;
      Serial.println("🚀 Logging Started");
    }
    else if (cmd == "STOP")
    {
      loggingActive = false;
      Serial.println("🛑 Logging Stopped");
    }
  }
  delay(100);
}