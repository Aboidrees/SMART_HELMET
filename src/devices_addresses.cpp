#include <Wire.h>
#include <SparkFun_Bio_Sensor_Hub_Library.h>

#define SDA_PIN      21
#define SCL_PIN      22
#define BIO_RST_PIN  25
#define BIO_MFIO_PIN 33

SparkFun_Bio_Sensor_Hub bioHub(BIO_RST_PIN, BIO_MFIO_PIN);

void scanI2C() {
  Serial.println("\n=== I2C Bus Scan ===");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("  0x"); Serial.print(addr, HEX);
      if (addr == 0x36) Serial.print(" -> MAX17048 Fuel Gauge");
      if (addr == 0x55) Serial.print(" -> SparkFun Bio Sensor Hub (MAX32664)");
      if (addr == 0x68) Serial.print(" -> MPU6050 IMU");
      if (addr == 0x3C || addr == 0x3D) Serial.print(" -> OLED Display (SSD1306)");
      if (addr == 0x48) Serial.print(" -> ADS1115 / TMP102");
      if (addr == 0x72) Serial.print(" -> ICM42688 / Unknown");
      if (addr == 0x77 || addr == 0x76) Serial.print(" -> BMP280 / BME280");
      Serial.println();
      found++;
    }
  }
  if (found == 0) Serial.println("  No devices found!");
  Serial.print("Total: "); Serial.print(found); Serial.println(" device(s)\n");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(SDA_PIN, SCL_PIN);

  scanI2C();

  Serial.println("=== Bio Sensor Hub ===");
  int result = bioHub.begin();
  Serial.print("begin() = "); Serial.println(result);
  Serial.println(result == 0 ? "OK" : "FAILED");
}

void loop() {}

