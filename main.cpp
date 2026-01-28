#include <Arduino.h>
#include "HR_Sensor.h"
#include "TEMP_Sensor.h"
#include "BP.h"

// Button tracking for HR/TEMP system
bool lastD6State = HIGH;  // Changed to D6 to avoid GPIO 2 conflict with BP

// BP sensor status
// (Moved flag to BP.h)

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("SETUP START");

    //=============================================================================
    // BP SETUP
    //=============================================================================
    
    //initialization
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
    
    // BP sensor initialization (non-blocking)
    if (!mpr.begin()) {
        Serial.print("Pressure sensor check failed - BP disabled\n");
        Serial.flush();
        bpSensorReady = false;
    } else {
        Serial.print("Pressure sensor check passed\n");
        Serial.flush();
        bpSensorReady = true;
    }
    //=============================================================================
    
    //initialize HR and TEMP after BP (to avoid I2C conflicts)
    delay(200);  
    HR_init();
    TEMP_init();
    
    Serial.println("\n=== READY ===");
    Serial.println("Press D6 to start HR+TEMP measurements");
    Serial.println("OR press D4 for HR only");
    Serial.println("BP uses A0 button (from BP.h code)");
    Serial.println("\nNOTE: D2 is used by BP system (in4), don't use for buttons!");
}

void loop() {
    //=============================================================================
    // BP TASKS - Run continuously if sensor is ready
    //=============================================================================
    if(bpSensorReady){
        currentmillis = millis();
        for (unsigned int i = 0; i < NUM_TASKS; i++) {
            if (currentmillis - tasks[i].elapsedTime >= tasks[i].period) {
                tasks[i].state = tasks[i].TickFct(tasks[i].state);
                tasks[i].elapsedTime = currentmillis;
            }
        }
    }
    //=============================================================================

    //HR sensor start loop based on button, will be changed to call from LLM or something
    bool currentD6 = digitalRead(7);
    if (lastD6State == HIGH && currentD6 == LOW) {
        delay(50);
        
        Serial.println("\n======================================");
        Serial.println("D6 PRESSED - STARTING HR+TEMP SENSORS");
        Serial.println("======================================");
        
        TEMP_startMeasurement();
        HR_startMeasurement();
    }
    lastD6State = currentD6;
    
    // Update HR and TEMP sensors (non-blocking)
    TEMP_update();
    HR_update();

    delay(10);

    // Sending to LLM
    if (bpSensorReady) {
        live.BP_sys = systolic; 
        live.BP_dia = diastolic;
    }
}
