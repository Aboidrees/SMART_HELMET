#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <ArduinoJson.h>
#include "SparkFun_Bio_Sensor_Hub_Library.h"

// MAX17048 raw I2C @ 0x36
// The Adafruit library rejects this chip (VERSION register mismatch).
// Raw I2C formulas are verified against live hardware register dump.
#define MAX17048_ADDR  0x36
#define MAX17048_VCELL 0x02  // 1 LSB = 78.125 µV
#define MAX17048_SOC   0x04  // MSB = integer %, LSB/256 = fractional %
#define MAX17048_MODE  0x06  // write 0x4000 for Quick Start

// Returns 0xFFFF on I2C failure (device missing or removed)
static uint16_t max17048_read(uint8_t reg) {
  Wire.beginTransmission(MAX17048_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFFFF;
  if (Wire.requestFrom((uint8_t)MAX17048_ADDR, (uint8_t)2) != 2) return 0xFFFF;
  return ((uint16_t)Wire.read() << 8) | Wire.read();
}
static void max17048_write(uint8_t reg, uint16_t val) {
  Wire.beginTransmission(MAX17048_ADDR);
  Wire.write(reg);
  Wire.write((uint8_t)(val >> 8));
  Wire.write((uint8_t)(val & 0xFF));
  Wire.endTransmission();
}
// Returns false if chip not responding
static bool max17048_read_batt(float &voltage, float &percent) {
  uint16_t vcell = max17048_read(MAX17048_VCELL);
  uint16_t soc   = max17048_read(MAX17048_SOC);
  if (vcell == 0xFFFF || soc == 0xFFFF) return false;
  voltage = vcell * 0.000078125f;
  percent = (soc >> 8) + (float)(soc & 0xFF) / 256.0f;
  return true;
}

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
      float v, pct;
      if (max17048_read_batt(v, pct)) {
        g_batt_v   = v;
        g_batt_pct = constrain((int)pct, 0, 100);
      } else {
        // chip removed or bus error — stop reading until next reboot
        batteryFound = false;
        g_batt_v   = 0.0f;
        g_batt_pct = 0;
      }
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
  delay(500);  // let power rails settle on cold boot

  // 1. MAX17048 — raw ACK check at 0x36, then Quick Start
  Wire.beginTransmission(MAX17048_ADDR);
  batteryFound = (Wire.endTransmission() == 0);
  if (!batteryFound) {
    Serial.println("WARNING: MAX17048 not found at 0x36");
  } else {
    max17048_write(MAX17048_MODE, 0x4000);  // Quick Start: recalibrate SOC from OCV
    delay(200);
    Serial.println("✅ MAX17048 Fuel Gauge Ready");
  }

  // Raise clock to 400kHz for MPU6050 and Bio Hub
  Wire.setClock(400000);

  // 2. تهيئة باقي الحساسات
  if (!mpu.begin())
  {
    Serial.println("Failed to find MPU6050 chip");
    while (1)
    {
      delay(10);
    }
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