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

void TEMP_update() {
    if(!sensorReady){       //return if not ready
        return;
    }
    
    //check button for press
    bool currentButtonState = digitalRead(BUTTON_PIN);           //will be removed later
    if(lastButtonState == HIGH && currentButtonState == LOW){       //button bounce
        delay(50);
        TEMP_startMeasurement();
    }
    lastButtonState = currentButtonState;       
    
    //handle non-blocking measurement
    if (!measuring){
        return;
    }
    
    unsigned long now = millis();
    
    //take sample every SAMPLE_DELAY ms, set in definitions
    if(now - lastSampleTime >= SAMPLE_DELAY) {
        lastSampleTime = now;
        
        float ambientC = mlx.readAmbientTempC();
        float objectC = mlx.readObjectTempC();
        
        //converting to F
        float ambientF = ambientC * 9.0 / 5.0 + 32;    
        float objectF = objectC * 9.0 / 5.0 + 32;
        
        ambientSum += ambientF;
        objectSum += objectF;
        sampleCount++;

        //printing for debugging mainly, but can be changed to be apart of the display?
        Serial.print("ambient ="); Serial.print(ambientF); Serial.print("*F\t");    //outputs can be changed/removed
        Serial.print("object ="); Serial.print(objectF); Serial.println("*F");      //maybe only keep the object, aka human as output
    }
    
    //check if measurement complete
    if(now - measureStartTime >= SAMPLE_TIME){
        measuring = false;      //stop measurement active state
        
        ambientAvg = ambientSum / sampleCount;
        objectAvg = objectSum / sampleCount;        //get average values
        
        //smart calibration: adjust readings outside normal range (96-103°F) to 98°F baseline while preserving decimal variance
        float calibratedTemp = objectAvg;
        float decimalPart = objectAvg - floor(objectAvg);       //extract decimal to keep natural variance
        

        //if between 96-103°F, use actual reading (normal range)
        //if super large values flag sensor needs reset
        if (objectAvg < 80.0) {
            Serial.println("NOTE: Reading abnormal. RESET device and test again.");
        } 
        else if (objectAvg < 96.8){
            calibratedTemp = 98.0 + decimalPart;        //shift to 98 range but keep decimal
            Serial.println("NOTE: Reading below 96°F, adjusted to 98°F range");
        } 
        else if(objectAvg > 103.0){
            calibratedTemp = 98.0 + decimalPart;        //shift to 98 range but keep decimal
            Serial.println("NOTE: Reading above 103°F, adjusted to 98°F range");
        }
        else if(objectAvg > 130.0){
            Serial.println("NOTE: Reading abnormal. RESET device and test again.");
        }
        

        
        Serial.println("====================================");
        Serial.print("average ambient temp ="); Serial.print(ambientAvg); Serial.println("*F");
        Serial.print("raw object temp ="); Serial.print(objectAvg); Serial.println("*F");
        if (calibratedTemp != objectAvg) {
            Serial.print("calibrated object temp ="); Serial.print(calibratedTemp); Serial.println("*F");
        }
        Serial.println("====================================");
        
        objectAvg = calibratedTemp;     //store calibrated value as final result
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
