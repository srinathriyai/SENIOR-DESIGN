//Blood Pressure
//takes blood pressure for ___ seconds and generates ____ samples
//stores average blood pressure data as ____ variable (name)

#include <Arduino.h>
#include "Adafruit_MPRLS.h"

// FOR CIRCUIT DIAGRAM REFER TO: FEASIBILITY STUDY - BP MONITOR CIRCUIT DIAGRAM
// Initialize 2, 3, 4, 5, SCL, and SDA pins

// Activate pump
// Set pin 3 (In2) to high

// Activate solenoid
// Set pin 5 (In1) to high

// Take readings from BP sensor
// Read from pins SCL and SDA using I2C

#define RESET_PIN -1
#define EOC_PIN
Adafruit_MPRLS mpr = Adafruit_MPRLS(RESET_PIN, EOC_PIN);

int main() {
  // Doing in main without states for now to test basic interaction
  serial.begin
  if (!mpr.begin()) { // Calls mpr.begin() (see Adafruit_MPRLS::begin) and will output a 0 if it could not begin the sensor
    serial.print("Pressure sensor check failed")
  } else {
    serial.print("Pressure sensor check passed")
  }
  float pressure = mpr.readpressure();
  serial.println(pressure); // in hectopascals (hPa) // 133.32 hPa = 1 mmHg

  return 0;
}
