

#include <Wire.h>

#define SDA_PIN A4
#define SCL_PIN A5
#define PCAADDR 0x70

void pcaselect(uint8_t i) {
  if (i > 3) return;
  Wire.beginTransmission(PCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  Serial.println("PCA9546 I2C Scanner");
}

void loop() {
  for (uint8_t port = 0; port < 4; port++) {
    pcaselect(port);
    delay(10);

    Serial.print("\nScanning PCA port ");
    Serial.println(port);

    bool found = false;

    for (uint8_t addr = 1; addr < 127; addr++) {
      if (addr == PCAADDR) continue;

      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) {
        Serial.print("  Found I2C device at 0x");
        Serial.println(addr, HEX);
        found = true;
      }
    }

    if (!found) {
      Serial.println("  No devices found");
    }
  }

  delay(5000);
}
