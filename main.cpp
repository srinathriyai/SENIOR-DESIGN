#include <Arduino.h>
#include "HR_Sensor.h"
#include "TEMP_Sensor.h"

//tracking simultaneous trigger
bool lastD2State = HIGH;

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("SETUP START");

    HR_init();
    TEMP_init();
    
    Serial.println("\n=== READY ===");
    Serial.println("Press D2 to start BOTH measurements simultaneously");
    Serial.println("OR press D4 for HR only");
}

void loop() {

    bool currentD2 = digitalRead(2);    //checking button on D2 for press while another button is pressed
    if(lastD2State == HIGH && currentD2 == LOW){
        delay(50);
        
        Serial.println("\n======================================");
        Serial.println("D2 PRESSED - STARTING BOTH SENSORS");
        Serial.println("======================================");
        
        TEMP_startMeasurement();    //start temp sampling
        HR_startMeasurement();     //start HR/spO2 sampling
        
  
        //debugging 
       // Serial.println("Now press D4 button to start HR measurement");
    }
    lastD2State = currentD2;        //set to last state
    
    //update both sensors
    TEMP_update();
    HR_update();

    delay(10);
}
