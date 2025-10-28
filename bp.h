//Blood Pressure
//takes blood pressure for ___ seconds and generates ____ samples
//stores average blood pressure data as ____ variable (name)

#include "Adafruit_MPRLS.h"
#include "timerISR.h"
#include <stdlib.h>
#include <Arduino.h>

// FOR CIRCUIT DIAGRAM REFER TO: FEASIBILITY STUDY - BP MONITOR CIRCUIT DIAGRAM, DONE ON ARDUINO UNO
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

 // Doing in main without states for now to test basic interaction
int main() {
  int value = 0; // For reading inputs
  
  // Valve and Pump
  // Initialization (ARDUINO UNO)
  int in1 = 5; pinMode(in1, OUTPUT); digitalWrite(in1, LOW); // MOTOR OFF
  int in2 = 4; pinMode(in2, OUTPUT); digitalWrite(in2, LOW); // ^
  int in3 = 3; pinMode(in3, OUTPUT); digitalWrite(in3, LOW); // VALVE OFF
  int in4 = 2; pinMode(in4, OUTPUT); digitalWrite(in4, LOW); // ^
  delay(2000);
  // Valve
  digitalWrite(in1, HIGH); digitalWrite(in2, LOW); // VALVE ON
  serial.println("Valve Opened");
  // Pump
  digitalWrite(in3, HIGH); digitalWrite(in4, LOW); // PUMP ON
  delay(2000);
  serial.println("Motor Started");
  digitalWrite(in3, LOW); digitalWrite(in4, LOW); // PUMP OFF

  // Pressure Sensor
  serial.begin
  if (!mpr.begin()) { // Calls mpr.begin() (see Adafruit_MPRLS::begin) and will output a 0 if it could not begin the sensor
    serial.print("Pressure sensor check failed");
  } else {
    serial.print("Pressure sensor check passed");
  }
  float pressure = mpr.readpressure();
  serial.println(pressure); // in hectopascals (hPa) // 133.32 hPa = 1 mmHg

  return 0;
}
