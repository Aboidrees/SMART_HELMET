#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include "SparkFun_Bio_Sensor_Hub_Library.h"

// MAX17048 raw I2C @ 0x36
#define MAX17048_ADDR  0x36
#define MAX17048_VCELL 0x02  // 1 LSB = 78.125 µV
#define MAX17048_SOC   0x04  // MSB = integer %, LSB/256 = fractional %
#define MAX17048_MODE  0x06  // write 0x4000 for Quick Start

// الأقطاب (Pins)
#define SDA_PIN 21
#define SCL_PIN 22
#define BIO_RST_PIN 25
#define BIO_MFIO_PIN 33
#define BUZZER_PIN 12
#define BUTTON_PIN 32

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

// Simple beep function
static void beep(int count = 1, int duration = 100, int pause = 150) {
  for (int i = 0; i < count; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(duration);
    digitalWrite(BUZZER_PIN, LOW);
    if (i < count - 1) delay(pause);
  }
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

// Global objects
Adafruit_MPU6050 mpu;
SparkFun_Bio_Sensor_Hub bioHub(BIO_RST_PIN, BIO_MFIO_PIN);

// Global variables
volatile int16_t g_ax, g_ay, g_az;
volatile int g_hr, g_spo2, g_conf, g_status;
volatile uint32_t g_ir, g_red;
volatile float g_batt_v;
volatile int g_batt_pct;

bool batteryFound = false;
unsigned int testNumber = 1;

void setup()
{
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);  // active HIGH: pressed = HIGH
  delay(1000);
  
  Serial.println("\n========== SENSOR TEST (NO WiFi) ==========");
  Serial.println("Testing sensors on battery power only");
  Serial.println("=========================================\n");
  
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(500);  // let power rails settle

  // 1. MAX17048 — raw ACK check at 0x36
  Wire.beginTransmission(MAX17048_ADDR);
  batteryFound = (Wire.endTransmission() == 0);
  if (!batteryFound) {
    Serial.println("WARNING: MAX17048 not found at 0x36");
  } else {
    max17048_write(MAX17048_MODE, 0x4000);
    delay(200);
    Serial.println("✅ MAX17048 Fuel Gauge Ready");
  }

  // Raise clock to 400kHz for MPU6050 and Bio Hub
  Wire.setClock(400000);

  // 2. Initialize MPU6050
  if (!mpu.begin()) {
    Serial.println("FAIL: MPU6050 not found");
    while (1) {
      delay(10);
    }
  }
  Serial.println("✅ MPU6050 Initialized");

  // 3. Initialize Bio Sensor
  if (bioHub.begin() == 0) {
    bioHub.configSensorBpm(MODE_TWO);
    bioHub.setPulseWidth(411);
    bioHub.setAdcRange(4096);
    Serial.println("✅ Bio Sensor Initialized");
  }

  Serial.println("\n📊 Reading sensors (no WiFi, no upload)...\n");
  
  // Startup beep notification
  beep(2, 150, 200);
  Serial.println("✅ Startup beep signaled\n");
}

void loop()
{
  // 1. Read IMU
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  g_ax = (int16_t)(a.acceleration.x * 1000);
  g_ay = (int16_t)(a.acceleration.y * 1000);
  g_az = (int16_t)(a.acceleration.z * 1000);

  // 2. Read Bio Sensor
  bioData body = bioHub.readSensorBpm();
  g_hr = body.heartRate;
  g_spo2 = body.oxygen;
  g_conf = body.confidence;
  g_status = body.status;
  g_ir = body.irLed;
  g_red = body.redLed;

  // 3. Read Battery
  if (batteryFound) {
    float v, pct;
    if (max17048_read_batt(v, pct)) {
      g_batt_v   = v;
      g_batt_pct = constrain((int)pct, 0, 100);
    } else {
      batteryFound = false;
      g_batt_v   = 0.0f;
      g_batt_pct = 0;
    }
  }

  // Print to serial every 2 seconds
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 2000) {
    lastPrint = millis();
    
    Serial.print("[T:");
    Serial.print(millis() / 1000);
    Serial.print("s] [IMU] ");
    Serial.print("AX=");
    Serial.print(g_ax);
    Serial.print(" AY=");
    Serial.print(g_ay);
    Serial.print(" AZ=");
    Serial.print(g_az);
    
    Serial.print(" | [PPG] ");
    Serial.print("HR=");
    Serial.print(g_hr);
    Serial.print(" SPO2=");
    Serial.print(g_spo2);
    Serial.print(" CONF=");
    Serial.print(g_conf);
    
    Serial.print(" | [BATT] ");
    Serial.print(g_batt_pct);
    Serial.print("% (");
    Serial.print(g_batt_v);
    Serial.println("V)");
  }

  delay(20);  // 50Hz sensor read rate

  // Button: start new test on press
  static bool lastButtonState = false;
  bool buttonPressed = (digitalRead(BUTTON_PIN) == HIGH);
  if (buttonPressed && !lastButtonState) {
    testNumber++;
    Serial.println();
    Serial.print(">>> NEW TEST STARTED: test_");
    Serial.println(testNumber);
    Serial.println();
    beep(1, 100);  // single beep to confirm
  }
  lastButtonState = buttonPressed;
}
