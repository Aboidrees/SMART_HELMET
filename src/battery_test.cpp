// ===================== MAX17048 Battery Test @ 0x36 =====================
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MAX1704X.h>

#define SDA_PIN 21
#define SCL_PIN 22

Adafruit_MAX17048 maxlipo;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  Serial.println("=== MAX17048 Battery Test @ 0x36 ===");

  if (!maxlipo.begin()) {
    Serial.println("ERROR: MAX17048 not found at 0x36! Check wiring.");
    while (1) delay(1000);
  }

  Serial.printf("Chip Version: 0x%02X\n", maxlipo.getChipID());

  // Quick Start: forces ModelGauge to re-estimate SOC from open-circuit voltage.
  // Fixes incorrect SOC readings after cold boot.
  Serial.println("Issuing Quick Start — recalibrating SOC from OCV...");
  maxlipo.quickStart();
  delay(200);  // IC needs ~174ms after QS for first valid SOC

  Serial.println("Ready.\n");
}

void loop() {
  Serial.printf("VOLT=%.3fV  SOC=%.1f%%  RATE=%+.2f%%/hr\n",
    maxlipo.cellVoltage(),
    maxlipo.cellPercent(),
    maxlipo.chargeRate());
  delay(2000);
}
