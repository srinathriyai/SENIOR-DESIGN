#include <Arduino.h>
#include "HR_Sensor.h"
#include "TEMP_Sensor.h"
#include "BP.h"

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
    
    //=============================================================================
    // BP SETUP
    //=============================================================================
    // Initialization (ESP 32)
    pinMode(in1, OUTPUT); digitalWrite(in1, LOW); // RELEASE ON
    pinMode(in2, OUTPUT); digitalWrite(in2, LOW); // ^
    pinMode(in3, OUTPUT); digitalWrite(in3, LOW); // MOTOR OFF
    pinMode(in4, OUTPUT); digitalWrite(in4, LOW); // ^

    unsigned char i = 0;
    tasks[i].elapsedTime = sample_pressure_PERIOD;
    tasks[i].period = sample_pressure_PERIOD;
    tasks[i].state = sample_pressure_INIT;
    tasks[i].TickFct = &tick_sample_pressure;
    ++i;
    tasks[i].elapsedTime = release_valve_PERIOD;
    tasks[i].period = release_valve_PERIOD;
    tasks[i].state = release_valve_INIT;
    tasks[i].TickFct = &tick_release_valve;
    ++i;
    tasks[i].elapsedTime = air_pump_PERIOD;
    tasks[i].period = air_pump_PERIOD;
    tasks[i].state = air_pump_INIT;
    tasks[i].TickFct = &tick_air_pump;
    ++i;
    tasks[i].elapsedTime = start_button_PERIOD;
    tasks[i].period = start_button_PERIOD;
    tasks[i].state = start_button_INIT;
    tasks[i].TickFct = &tick_start_button;

    Serial.flush();
    
    if (!mpr.begin()) { // Calls mpr.begin() (see Adafruit_MPRLS::begin) and will output a 0 if it could not begin the sensor
        Serial.print("Pressure sensor check failed\n");
        Serial.flush();
        while(1) {
        Serial.print("Stopped, Restart\n");
        Serial.flush();
        delay(1000);
        }
    } else {
        Serial.print("Pressure sensor check passed\n");
        Serial.flush();
    }
    //=============================================================================
}

void loop() {
    //=============================================================================
    // BP TASKS
    //=============================================================================
    currentmillis = millis();
    for (unsigned int i = 0; i < NUM_TASKS; i++) {
        if (currentmillis - tasks[i].elapsedTime >= tasks[i].period) {
        tasks[i].state = tasks[i].TickFct(tasks[i].state); // Runs the tick and then sets the output state to the new state.
        tasks[i].elapsedTime = currentmillis;
        }
    }
    //=============================================================================
    
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
