#ifndef HR_SENSOR_H
#define HR_SENSOR_H

#include <Arduino.h>
#include <Wire.h>
#include <MAX30105.h>

//Board PIN definitions, I2C
//ESP32 uses GPIO 21 (SDA) and GPIO 22 (SCL) as default I2C pins, same for arduino brand
#define I2C_SDA 21
#define I2C_SCL 22

//button pin for triggering measurements, will be changed or removed after debugging etc
//using INPUT_PULLUPso button connects pin to GND when pressed
#define BUTTON_PIN 12

//object for sensor
MAX30105 particleSensor;

//DC Baseline stracking globals
//DC (steady baseline):average light absorption through tissue
//AC (pulsatile): small variations caused by blood flow
//track DC separately so  AC component can be extracted (heart beat)
float irDC = 0;                 //infrared DC baseline
float redDC = 0;                    //red LED DC baseline
const float DC_ALPHA = 0.98f;  //exponential moving average factor, needed for slow tracking so DC doesnt overwrite

//Finger detection and timing global variables
const uint32_t IR_THRESHOLD = 50000;  //min IR value indicating finger is present, triggers not there if anything belo

const uint16_t START_DELAY = 3000;    //wait 3 seconds after finger detected before measuring, help stablilize DC baseline
const uint8_t SAMPLE_DELAY = 20;      //20ms between samples = 50Hz sampling rate, detect 40-180BPM
const uint16_t MEASUREMENT_TIME = 20000;  //20 sec measurement window

bool fingerOnSensor = false;          //flag for if finger detected
uint32_t fingerDetectedTime = 0;      //stamp   when finger was first detected
bool measurementStarted = false;        //flag for if 20s measurement began
uint32_t startTime = 0;                  //stamp when measurement started

//AC signal processing variable ____ IF ANY ISSUES try different valuesto adjust
const float AC_SCALE = 1.0f;  //no scaling, I had 0.1 before and it wouldnt detect all the time

//Heart beat detection globals
//uses peak/valley detection with adaptive threshold
//couldnt use simple threshold crossing for accuracy
float beatThreshold = 100;              // Dynamically adjusted based on signal strength
const uint16_t MIN_BEAT_INTERVAL = 400;     //400ms = max 150 BPM (prevents double-counting)
const uint16_t MAX_BEAT_INTERVAL = 2000;    //2000ms = min 30 BPM, rejecting the noisy abnormal values

//peak/valley state machine variables for heart beat
float peakValue = 0;        //current peak candidate value
float valleyValue = 0;      //current valley candidate value
bool lookingForPeak = true; //state: searching for peak (true) or valley (false), hope ot prevent counting same beat multiple times

float lastIRAC = 0;         // Previous AC value (for derivative calculation)
uint32_t lastBeatTime = 0;  // Timestamp of last detected beat

//Smoothing BPM values globals
//raw beat intervals can vary ±10 BPM due to breathing, movement, etc causing alot of variability
//exponential moving average smooths these variations, from common past uses on google
float bpmFiltered = 0;           //smoothed BPM value
float currentHR = 1;            //value grabbed from main for LLM result
const float BPM_ALPHA = 0.3f;    //smoothing factor: 0.3 = responsive but smooth, too high before made response slower
uint8_t bpmCount = 0;            //tracking number of valid beats, determined in function later

//oxygen calculation globals, from the ratio of Red/IR pulsatile signals and accumulate AC values for avg
float irACSum = 0;          //sum of absolute IR AC values
float redACSum = 0;         //sum of absolute Red AC values
uint16_t acSampleCount = 0;         //no. of samples taken, absolute values later handle inverted signals, was causing issues before...

//smooth spO2 values globals
float spo2Smoothed = 0;           //smoothed oxygenpercentage
float currentO2 = 1;            //value grabbed from main for LLM
const float SPO2_ALPHA = 0.4f;      //smoothing factor,slightly higher than BPM for faster stabilization

//button handling globals
bool hrActive = false;                  //flag for measurement currently active
uint32_t lastButtonCheck = 0;                    //trackinglast time button was checked 
const uint8_t BUTTON_DEBOUNCE = 50;         //50ms debounce  for button

//Diagnostic globals, debugging signal quality issues with globals below
bool diagnosticMode = true;    //enable detailed diagnostic output every 2 seconds
uint32_t lastDiagnostic = 0;   //tracking time for diagnostic 
float maxIRAC = 0;             //max IR AC value, during measuring
float minIRAC = 0;             //min IR AC value , during measuring
float maxRedAC = 0;          //max Red AC value, during measuring
float minRedAC = 0;          //minimum Red AC value, during measuring
                                //range (max - min) indicates signal strength, from datasheet info

//Sampling
static unsigned long lastHRsample = 0;

//initialize MAX30102 and button
//port= 0..3 on the PCA9546 mux
void HR_init();

void HR_update();   //called in main.cpp loop()
void HR_startMeasurement();

//check if measurement is active, might be able to remove? Or needed for simultaenous 
bool HR_isActive();

void HR_init() {                    //Initializing
    //Serial.println("HR: init");  uncomment for debugging, but should be fine now

    //button configuration, for debugging/testing will change later
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    //I2C communication at 400kHz (fast mode), fastest supported mode
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);

    //attempt to initialize MAX30102 sensor, checks I2C communication and verify sensors working
    if(!particleSensor.begin(Wire, I2C_SPEED_FAST)){
        Serial.println("ERROR: MAX30102 not found!");
        //stop if not found
        while (true){
            delay(1000);
        }
    }

    //Sensor Configuration   
    byte ledBrightness = 60;   //LED current (0-255): 60 = best, too low gives weak too high causes browning power
    byte sampleAverage = 4;    //average 4 samples per reading, reduce noise
    byte ledMode = 2;          //mode 2 = Red + IR LEDs, from datasheet for both hr and oxygen sensing   
    int sampleRate = 400;      //400 samples/second, for accurate heart rate waveform 
    int pulseWidth = 411;      //411 microseconds pulse width, 18-bit resolution
    int adcRange = 16384;      //16384 (14-bit), higher res for small AC signals
    //apply configuration to sensor
    particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
    
    //setting LED currents to 60 for balanced Red/IR signal strength, too low won't get accurate signals etc
    particleSensor.setPulseAmplitudeRed(60);
    particleSensor.setPulseAmplitudeIR(60);

    //Serial.println("HR: MAX30102 ready");                    used for debugging, commented out for now
    //Serial.println("Press button to start measurement...");
}

void HR_startMeasurement(){ 
    if(!hrActive){
        hrActive = true;
        Serial.println("HR measurement triggered");
    }
}

//main loop ___ called repeatedly from main loop: for button handling, sampling and beat detection
void HR_update(){

    uint32_t now = millis();

    //chheck button with debouncing to prevent false triggers
    /*
    if(!hrActive && (now - lastButtonCheck >= BUTTON_DEBOUNCE)){
        if(digitalRead(BUTTON_PIN) == LOW) {  //button pressed (pulled to GND)
            lastButtonCheck = now;
            hrActive = true;
            Serial.println("Button pressed. Place finger...");
        }
    }
    */

    //exit early if no measurement is active
    // if(!hrActive){
    //     return; 
    // }
    if(now - lastHRsample >= SAMPLE_DELAY) { // Checks if it's time to execute update
      lastHRsample = now;
    } 
    else{
      return;
    }

    //read both IR and Red values from sensor, DC and AC components
    uint32_t ir = particleSensor.getIR();
    uint32_t red = particleSensor.getRed();

    //Finger detection, send somewhere when placed and removed etc
    if(ir > IR_THRESHOLD && !fingerOnSensor){
        fingerOnSensor = true;
        fingerDetectedTime = now;
        measurementStarted = false;
        
        //initialize DC baselines with first reading, start to exponential average
        irDC = ir;
        redDC = red;
        
        Serial.println("Finger detected. Stabilizing...");
        Serial.print("Initial IR reading: ");
        Serial.println(ir);
    }

    //detect when finger is removed, restart incase measurements corrupted/false
    if(ir < IR_THRESHOLD && fingerOnSensor){
        fingerOnSensor = false;
        measurementStarted = false;
        Serial.println("Finger removed.");
    }

    //tracking DC value, update baseline using exponential moving average
    //DC_ALPHA = 0.98 means: new_DC = 0.98 * old_DC + 0.02 * new_reading
    //tracking to mitigate fast heartbeat oscillations affecting result
    if(fingerOnSensor){
        irDC = DC_ALPHA*irDC + (1.0f - DC_ALPHA)*ir;
        redDC = DC_ALPHA *redDC + (1.0f - DC_ALPHA)*red;
    }

    //Take measurement
    //only start measuring after finger has been stable for START_DELAY ms
    if(fingerOnSensor && (now - fingerDetectedTime >= START_DELAY)){

        if(!measurementStarted){      //Initializing measurements
            measurementStarted = true;
            startTime = now;
            
            //measurement variables
            bpmFiltered = 0;
            bpmCount = 0;
            irACSum = 0;
            redACSum = 0;
            acSampleCount = 0;
            lastIRAC = 0;
            lastBeatTime = 0;
            spo2Smoothed = 0;
            
            //peak/valley detection
            peakValue = 0;
            valleyValue = 0;
            lookingForPeak = true;
            
            //reset original diagnostic tracking
            maxIRAC = -10000;
            minIRAC = 10000;
            maxRedAC = -10000;
            minRedAC = 10000;
            lastDiagnostic = now;
            
            Serial.println("Starting measurement...");
            Serial.println("Monitoring AC signal...");
        }

        //getting AC values, subtract DC baseline to get AC (heartbeat) signal
        float irAC = (float)ir - irDC;
        float redAC = (float)red - redDC;

        //track min/max AC values for diagnostics and adaptive threshold later in code
        if (irAC > maxIRAC) maxIRAC = irAC;
        if (irAC < minIRAC) minIRAC = irAC;
        if (redAC > maxRedAC) maxRedAC = redAC;
        if (redAC < minRedAC) minRedAC = redAC;

        //Output, can change later on and make different for display
        //print every 2 second for debuggin atm
        /*
        if(diagnosticMode && (now - lastDiagnostic >= 2000)){ 
            Serial.print("IR AC: ");
            Serial.print(minIRAC, 0);
            Serial.print(" to ");
            Serial.print(maxIRAC, 0);
            Serial.print(" | Red AC: ");
            Serial.print(minRedAC, 0);
            Serial.print(" to ");
            Serial.print(maxRedAC, 0);
            Serial.print(" | Beats: ");
            Serial.println(bpmCount);
            lastDiagnostic = now;
        }
*/
        //Making threshold adaptive to adjust for detection, based on read signal strength 
        //threshold = 30% of signal range works well across different people, autoshapes to weak/strong
        float signalRange = maxIRAC - minIRAC;
        if (signalRange > 50){
            beatThreshold = signalRange * 0.3f;
        }

        //high and low detection for heart beats (gonna call them as peak and valley)
        //using state machine type to prevent coutning wrong beats
        //looks for: peak → valley → peak → valley (one beat per cycle)
        
        if(lookingForPeak) {    //tracking the peak value
            if(irAC > peakValue){
                peakValue = irAC;   //update if higher val
            } 
            //tracking the falling/just passed the peak
            else if(irAC < peakValue - beatThreshold){

                lookingForPeak = false;     //set to show value is not a peak
                valleyValue = irAC;         
                
                uint32_t interval = now - lastBeatTime;     //validating the heart beat
                
                //check if beat interval is feasible, not super increase or decrease
                if(lastBeatTime > 0 && interval >= MIN_BEAT_INTERVAL && interval <= MAX_BEAT_INTERVAL){

                    float bpm = 60000.0f/interval;  //ms to beats per min
                    

                    if (bpm >= 40 && bpm <= 180){      //if valid for alive human
                        bpmCount++;
                        
                        //smoothing bpm, using exponential moving average for stable BPM reading
                        if (bpmFiltered == 0){
                            bpmFiltered = bpm;          //set first beat to initial
                        }
                        else{ //put new reading with average
                            float diff = abs(bpm - bpmFiltered) / bpmFiltered;
                            if(diff < 0.40f){
                                bpmFiltered = BPM_ALPHA*bpm + (1.0f - BPM_ALPHA)*bpmFiltered;
                            }
                        }
                        
                        //print beat notification
                        Serial.print("♥ Beat #");
                        Serial.print(bpmCount);
                        Serial.print(" | BPM: ");
                        Serial.print(bpmFiltered, 1);
                        Serial.print(" | Interval: ");
                        Serial.print(interval);
                        Serial.println("ms");
                    }
                }
                lastBeatTime = now;
            }
        } 
        else{
            //looking for valley, lower part of heartbeat
            if(irAC < valleyValue){
                valleyValue = irAC;     //update for lower value
            } 
            //rising phase: we've passed the valley
            else if(irAC > valleyValue + beatThreshold){        //
                
                lookingForPeak = true;  //set valley to found
                peakValue = irAC;
            }
        }

        lastIRAC = irAC;    //saves ac value for loop

        //get AC values for spO2
        irACSum += abs(irAC);     //cconvert to absolute values to handle both positive and negative AC signals, from issue of low values
        redACSum += abs(redAC);
        acSampleCount++;            //continue the sampling


        if (acSampleCount > 0 && bpmCount >= 5) {  //for every 5 beats calculate spo2
            float irACAvg = irACSum / acSampleCount;
            float redACAvg = redACSum / acSampleCount;
    
            float R = (redACAvg / irACAvg) * (irDC / redDC);
            float spo2 = -45.060f * R * R + 30.354f * R + 94.845f;
            spo2 = constrain(spo2, 85, 100);
    
            if(spo2Smoothed == 0){
                spo2Smoothed = spo2;
            } 
            else{
                spo2Smoothed = SPO2_ALPHA * spo2 + (1.0f - SPO2_ALPHA) * spo2Smoothed;
            }
            }
        }

        //serial output
        if(diagnosticMode && (now - lastDiagnostic >= 2000)) {  //interval for outputting serial CAN BE REMOVED on final
            lastDiagnostic = now;
            if(bpmFiltered > 0){
                Serial.print("Average BPM: ");
                Serial.println(bpmFiltered, 1);
            }
            if(spo2Smoothed > 0){
                Serial.print("Smoothed SpO2: ");
                Serial.print(spo2Smoothed, 1);
                Serial.println("%");
            }
            Serial.print("Beat count: ");
            Serial.println(bpmCount);
        }

        //IF measurements time ends
        // if(now - startTime >= MEASUREMENT_TIME) { 
        //     //hrActive = false;           //turn off active stuff
        //     //fingerOnSensor = false;  //removed for continous looping unless finger is removed
        //     measurementStarted = false;

        //     Serial.println("\n========== MEASUREMENT COMPLETE ==========");
        //     Serial.print("Total beats detected: ");
        //     Serial.println(bpmCount);
        //     /*
        //     Serial.print("IR AC range: ");
        //     Serial.print(minIRAC, 0);
        //     Serial.print(" to ");
        //     Serial.println(maxIRAC, 0);
        //     Serial.print("Red AC range: ");
        //     Serial.print(minRedAC, 0);
        //     Serial.print(" to ");
        //     Serial.println(maxRedAC, 0);
        //     */

        //     //oxygen calcuation (spO2)
        //     if(bpmCount >= 5){        //min5 beats for reliable measurement
        //         if(acSampleCount > 0){


        //             //if(spo2Smoothed < 90){
        //             //    spo2Smoothed = 0;
        //             //}

        //             //output data
        //             Serial.print("Average BPM: ");
        //             Serial.println(bpmFiltered, 1);
        //             /*
        //             Serial.print("R-value: ");
        //             Serial.println(R, 3);
        //             */
        //             Serial.print("Calculated SpO2: ");
        //             Serial.print(spo2, 1);
        //             Serial.println("%");
        //             Serial.print("Smoothed SpO2: ");
        //             Serial.print(spo2Smoothed, 1);
        //             Serial.println("%");
        //         }
        //     }
        //     else{
        //         Serial.println("WARNING: Insufficient beats detected");
        //     }
            //Serial.println("===========================================\n");
            //Serial.println("Press button to measure again.");
            
        //}
    //}
}

float HR_getMeasurement(){
    if(bpmFiltered == 0){
        currentHR = 0;
    }
    else currentHR = bpmFiltered;

    return currentHR;
}

float O2_getMeasurement(){
    if(spo2Smoothed == 0){
        currentO2 = 0;
    }
    else currentO2 = spo2Smoothed;

    return currentO2;
}

bool HR_isActive() { //main.cpp checks if sampling in progress
    return hrActive;
}

#endif
