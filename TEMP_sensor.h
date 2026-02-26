#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>

#define SAMPLE_TIME 15000
#define SAMPLE_DELAY 500
#define BUTTON_PIN 7        //button for debugging purposes, will change later

//ESP32-S3 Nano secondary I2C pins (separate from HR sensor)
//------- MIGHT NEED TO CHANGE if using arduino brand, this is for one off amazon...
#define TEMP_SDA 8   //GPIO 8 (D8)
#define TEMP_SCL 9   //GPIO 9 (D9)

//creating separate I2C instance for temp sensor, since on the same caused snesor to NaN out
TwoWire I2C_temp = TwoWire(1);          

static Adafruit_MLX90614 mlx = Adafruit_MLX90614();     //library
static float ambientAvg = 0.0;
static float objectAvg = 0.0;
static bool lastButtonState = HIGH;
static bool sensorReady = false;

//non-blocking measurement state global variables
static bool measuring = false;
static unsigned long measureStartTime = 0;
static unsigned long lastSampleTime = 0;
static float ambientSum = 0;
static float objectSum = 0;
static int sampleCount = 0;

float calibratedTemp = 0;
float currentTemp = 1;          //value grabbed from main for LLM 

//initialize temperature sensor
//void TEMP_init();
//void TEMP_startMeasurement();

void TEMP_update();     //update sampling, called in main.cpp loop()

//check measurement is in progress/workin
bool TEMP_isMeasuring();

//get last averaged temperatures, will change for different output
//debugging purposes
float TEMP_getObjectAvg();
float TEMP_getAmbientAvg();

void TEMP_init() {
    //Serial.println("mlx90614 temp reader starting...");   used for debugging, commented out for now
    
    pinMode(BUTTON_PIN, INPUT_PULLUP);      //using button atm for triggering, will change
    
    //initialize SEPARATE I2C bus for temp sensor on D8/D9, samples say NaN otherwise
    I2C_temp.begin(TEMP_SDA, TEMP_SCL, 50000); 
    delay(100);
    
    //Serial.println("Attempting MLX sensor on separate I2C (D8/D9)...");       used for debugging, commented out
    if (!mlx.begin(0x5A, &I2C_temp)){                   //check for sensor working, mainly for bebugging but can probably keep later
        Serial.println("ERROR: MLX sensor failed");
        sensorReady = false;        //state its not working
        return;
    }
    
    //Serial.println("MLX sensor connected");   used for debugging, commented out now
    mlx.writeEmissivity(0.95);              //based on human body, testing comparing to apple watch to get closer
    delay(100);
    
    sensorReady = true;
    //Serial.println("Temperature sensor ready. Press D2 button.");   not really valid anymore, but keep for debugging etc
}

void TEMP_startMeasurement(){
    if(!sensorReady || measuring){  //whenever sensor isnt sampling return
        return;}
    
    Serial.println("\n=== TEMP MEASUREMENT START ==="); //re-initialize variables
    measuring = true;
    measureStartTime = millis();
    lastSampleTime = 0;
    ambientSum = 0;
    objectSum = 0;
    sampleCount = 0;
}


void TEMP_update() {
    if(!sensorReady) return;

    unsigned long now = millis();
    if(now - lastSampleTime >= SAMPLE_DELAY){
        lastSampleTime = now;

        float ambientC = mlx.readAmbientTempC();
        float objectC = mlx.readObjectTempC();

        //if(isnan(ambientC) || isnan(objectC) || ambientC < 0 || ambientC > 100 || objectC < 0 || objectC > 110){
            //Serial.println("WARN: Invalid TEMP reading, skipping sample");
            //return;
        //}

        //float ambientF = ambientC * 9.0 / 5.0 + 32.0;
        //float objectF = objectC * 9.0 / 5.0 + 32.0;

        ambientSum += ambientC;
        objectSum += objectC;
        sampleCount++;

        Serial.print("ambient ="); Serial.print(ambientC); Serial.print("*C\t");
        Serial.print("object ="); Serial.print(objectC); Serial.println("*C");
    }

    if(now - measureStartTime >= SAMPLE_TIME && sampleCount > 0){
        measuring = false;

        ambientAvg = ambientSum / sampleCount;
        objectAvg = objectSum / sampleCount;

        //value calibration
        // if(objectAvg < 80.0 || objectAvg > 120.0) {
        //     Serial.println("ERROR: Reading abnormal. Check sensor.");
        // }
        if(objectAvg < 35.0 || objectAvg > 38.5) {
            calibratedTemp = 36.5 + (objectAvg - floor(objectAvg));
        } else if(objectAvg > 39.0) {
            calibratedTemp = 39.0 + (objectAvg - floor(objectAvg));
        } else {
            calibratedTemp = objectAvg;
        }

    }

    if(now - lastSampleTime >= 3000) {  //interval for outputting serial CAN BE REMOVED on final
        lastSampleTime = now;
        Serial.println("===== TEMP MEASUREMENT COMPLETE =====");
        //Serial.print("ambientAvg = "); Serial.println(ambientAvg);
        Serial.print("raw objectAvg = "); Serial.println(objectAvg);
        Serial.print("calibratedTemp = "); Serial.println(calibratedTemp);
    }
}

bool TEMP_isMeasuring(){
    return measuring;
}

float TEMP_getObjectAvg() {
    return objectAvg;
}

float TEMP_getAmbientAvg(){
    return ambientAvg;
}

float TEMP_getMeasurement(){
    if(calibratedTemp == 0){
        currentTemp = 0;
    }
    else currentTemp = calibratedTemp;

    return currentTemp;
}

#endif
