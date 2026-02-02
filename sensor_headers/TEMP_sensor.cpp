#include "TEMP_Sensor.h"

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

void TEMP_init() {
    Serial.println("mlx90614 temp reader starting...");
    
    pinMode(BUTTON_PIN, INPUT_PULLUP);      //using button atm for triggering, will change
    
    //initialize SEPARATE I2C bus for temp sensor on D8/D9, samples say NaN otherwise
    I2C_temp.begin(TEMP_SDA, TEMP_SCL, 100000); 
    delay(100);
    
    Serial.println("Attempting MLX sensor on separate I2C (D8/D9)...");
    if (!mlx.begin(0x5A, &I2C_temp)){                   //check for sensor working, mainly for bebugging but can probably keep later
        Serial.println("ERROR: MLX sensor failed");
        sensorReady = false;        //state its not working
        return;
    }
    
    Serial.println("MLX sensor connected");
    mlx.writeEmissivity(0.95);              //based on human body, testing comparing to apple watch to get closer
    delay(100);
    
    sensorReady = true;
    Serial.println("Temperature sensor ready. Press D2 button.");   //button might change etc etc
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

float TEMP_getMeasurement(){
    if(calibratedTemp == 0){
        currentTemp = 0;
    }
    else currentTemp = calibratedTemp;

    return currentTemp;
}

void TEMP_update() {
    if(!sensorReady || !measuring) return;

    unsigned long now = millis();
    if(now - lastSampleTime >= SAMPLE_DELAY){
        lastSampleTime = now;

        float ambientC = mlx.readAmbientTempC();
        float objectC = mlx.readObjectTempC();

        if(isnan(ambientC) || isnan(objectC)) {
            Serial.println("NaN reading, skipping");
            return;
        }

        float ambientF = ambientC * 9.0 / 5.0 + 32.0;
        float objectF = objectC * 9.0 / 5.0 + 32.0;

        ambientSum += ambientF;
        objectSum += objectF;
        sampleCount++;

        Serial.print("ambient ="); Serial.print(ambientF); Serial.print("*F\t");
        Serial.print("object ="); Serial.print(objectF); Serial.println("*F");
    }

    if(now - measureStartTime >= SAMPLE_TIME && sampleCount > 0){
        measuring = false;

        ambientAvg = ambientSum / sampleCount;
        objectAvg = objectSum / sampleCount;

        //value calibration
        if(objectAvg < 80.0 || objectAvg > 120.0) {
            Serial.println("ERROR: Reading abnormal. Check sensor.");
            calibratedTemp = 0;
        } else if(objectAvg < 96.0) {
            calibratedTemp = 96.0 + (objectAvg - floor(objectAvg));
        } else if(objectAvg > 103.0) {
            calibratedTemp = 103.0 + (objectAvg - floor(objectAvg));
        } else {
            calibratedTemp = objectAvg;
        }

        Serial.println("=== TEMP MEASUREMENT COMPLETE ===");
        Serial.print("ambientAvg = "); Serial.println(ambientAvg);
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
