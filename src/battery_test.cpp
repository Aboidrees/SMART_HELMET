// ===================== I2C Scanner + MAX17048 Test =====================
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MAX1704X.h>

#define SDA_PIN 21
#define SCL_PIN 22

Adafruit_MAX17048 maxlipo;

void scanI2C() {
  Serial.println("\n=== I2C Bus Scan ===");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("  0x"); Serial.print(addr, HEX);
      // Known device labels
      if (addr == 0x36) Serial.print(" -> MAX17048 Fuel Gauge");
      if (addr == 0x55) Serial.print(" -> SparkFun Bio Sensor Hub (MAX32664)");
      if (addr == 0x68) Serial.print(" -> MPU6050 IMU");
      if (addr == 0x3C || addr == 0x3D) Serial.print(" -> OLED Display (SSD1306)");
      if (addr == 0x48) Serial.print(" -> ADS1115 / TMP102");
      if (addr == 0x72) Serial.print(" -> *** Unknown - probing...");
      Serial.println();
      found++;
    }
  }
  if (found == 0) Serial.println("  No devices found!");
  Serial.print("Total devices found: "); Serial.println(found);
}

void probe0x72() {
  Serial.println("\n=== Probing 0x72 ===");
  // Try reading version/ID register - common registers across gauge ICs
  uint8_t regs[] = {0x00, 0x01, 0x02, 0x03, 0x08, 0x0E, 0x0F, 0xFE, 0xFF};
  for (uint8_t i = 0; i < sizeof(regs); i++) {
    Wire.beginTransmission(0x72);
    Wire.write(regs[i]);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)0x72, (uint8_t)2);
    if (Wire.available() >= 2) {
      uint8_t hi = Wire.read();
      uint8_t lo = Wire.read();
      Serial.printf("  REG 0x%02X = 0x%02X%02X (%d)\n", regs[i], hi, lo, (hi << 8) | lo);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(SDA_PIN, SCL_PIN);

  scanI2C();
  probe0x72();

  Serial.println("\n=== MAX17048 @ 0x36 ===");
  if (!maxlipo.begin()) {
    Serial.println("MAX17048 NOT found at 0x36.");
    Serial.println("Your fuel gauge may be at a different address or be a different chip.");
  } else {
    Serial.print("MAX17048 found! Chip ID: 0x");
    Serial.println(maxlipo.getChipID(), HEX);
  }
}

void loop() {
  if (maxlipo.begin()) {
    Serial.printf("VOLT=%.3fV  PCT=%.1f%%  RATE=%.2f%%/hr\n",
      maxlipo.cellVoltage(),
      maxlipo.cellPercent(),
      maxlipo.chargeRate());
  }
  delay(2000);
}
