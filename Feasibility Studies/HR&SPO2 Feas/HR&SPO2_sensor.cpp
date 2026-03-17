//Heart Rate + SpO2 Sensor block
//takes heart rate/pulse for 30 seconds and generates 500 samples
//stores average heart rate data as ____bpmFiltered____ variable (name)
//stores average SpO2 as ____spo2Smoothed_____ variable (name)
//plan to change variables later with LLM application

//code edited+revised from sparkfun arduino library

#include <Wire.h>
#include "MAX30105.h"

MAX30105 particleSensor;

//dc baseline values that track the average signal to get the ac part
float irDC = 0;
float redDC = 0;

//finger detection and timing setup
#define IR_THRESHOLD 50000       //finger detection threshold value (adjust if needed)
#define START_DELAY 3000         //wait 3 seconds after finger placed to stabilize
#define SAMPLE_DELAY 40          //~25hz sample rate
#define MEASUREMENT_TIME 30000   //measure for 30 seconds total

bool fingerOnSensor = false;
unsigned long fingerDetectedTime = 0;
bool measurementStarted = false;
unsigned long startTime = 0;

//ac scaling factor to make numbers smaller for easier reading
#define AC_SCALE 0.1             //scale factor for debugging

//beat detection parameters
#define BEAT_THRESHOLD 50        //how big the ac swing must be to count a beat
#define MIN_BEAT_INTERVAL 500    //min time (ms) between beats (~120 bpm max)

float lastIRAC = 0;
unsigned long lastBeatTime = 0;

//bpm smoothing filter
float bpmFiltered = 0;
float bpmAlpha = 0.2;
int bpmCount = 0;

//sums for average ir/red values
float irSum = 0;
float redSum = 0;
unsigned int sampleCount = 0;

//spo2 smoothing filter (exponential moving average)
float spo2Smoothed = 0;
float spo2Alpha = 0.3;

void setup() {
  Serial.begin(9600);
  if (!particleSensor.begin()) {
    Serial.println("MAX30102 not found! check wiring/power.");
    while (1);
  }

  particleSensor.setup();
  particleSensor.setPulseAmplitudeIR(50);   //turn on ir led
  particleSensor.setPulseAmplitudeRed(50);  //turn on red led

  Serial.println("place finger on sensor to start...");
}

void loop() {
  long ir = particleSensor.getIR();   //read ir light amount
  long red = particleSensor.getRed(); //read red light amount

  //check if a finger is placed on the sensor
  if (ir > IR_THRESHOLD && !fingerOnSensor) {
    fingerOnSensor = true;
    fingerDetectedTime = millis();  //record when finger first detected
    measurementStarted = false;
    irDC = ir;
    redDC = red;
    Serial.println("finger detected, waiting 3 seconds for signal to stabilize...");
  }

  //check if finger is removed
  if (ir < IR_THRESHOLD && fingerOnSensor) {
    fingerOnSensor = false;
    measurementStarted = false;
    Serial.println("finger removed, measurement stopped.\n");
  }

  //update baseline dc levels (slowly follows overall brightness)
  irDC = 0.9 * irDC + 0.1 * ir;
  redDC = 0.9 * redDC + 0.1 * red;

  //once finger has been on long enough, start measuring
  if (fingerOnSensor && (millis() - fingerDetectedTime >= START_DELAY)) {
    if (!measurementStarted) {
      measurementStarted = true;
      startTime = millis();
      bpmFiltered = 0;
      bpmCount = 0;
      irSum = 0;
      redSum = 0;
      sampleCount = 0;
      lastIRAC = 0;
      lastBeatTime = 0;
      spo2Smoothed = 0;
      Serial.println("starting 30-second measurement...");
    }

    //calculate ac (changing part of signal)
    float irAC = (ir - irDC) * AC_SCALE;
    float redAC = (red - redDC) * AC_SCALE;

    //print debug info
    Serial.print("IR="); Serial.print(ir);
    Serial.print(" RED="); Serial.print(red);
    Serial.print(" IR AC="); Serial.println(irAC);

    //beat detection using zero-cross and thresholds
    if ((irAC > BEAT_THRESHOLD && lastIRAC <= BEAT_THRESHOLD) ||
        (irAC < -BEAT_THRESHOLD && lastIRAC >= -BEAT_THRESHOLD)) {
      unsigned long now = millis();
      if (lastBeatTime > 0 && (now - lastBeatTime) >= MIN_BEAT_INTERVAL) {
        float bpm = 60000.0 / (now - lastBeatTime); //convert ms to bpm
        if (bpm >= 40 && bpm <= 180) { //only count realistic heart rates
          bpmCount++;
          if (bpmFiltered == 0) bpmFiltered = bpm; //first reading
          else bpmFiltered = bpmAlpha * bpm + (1 - bpmAlpha) * bpmFiltered; //low pass filter
          Serial.print("beat detected! bpm="); Serial.println(bpmFiltered, 1);
        }
      }
      lastBeatTime = now;
    }
    lastIRAC = irAC;

    //add ir and red values to sum for averaging later
    irSum += ir;
    redSum += red;
    sampleCount++;

    //check if 30 seconds passed yet
    if (millis() - startTime >= MEASUREMENT_TIME) {
      measurementStarted = false;
      fingerOnSensor = false;

      //compute averages
      float irAvg = irSum / sampleCount;
      float redAvg = redSum / sampleCount;

      //approximate spo2 using ratio of ac/dc components
      float irACAvg = (irAvg - irDC) * AC_SCALE;
      float redACAvg = (redAvg - redDC) * AC_SCALE;
      float ratio = redACAvg / (irACAvg + 0.001); //add small value to avoid divide by zero
      float spo2 = 104 - 17 * ratio; //basic spo2 estimation equation
      if (spo2 > 100) spo2 = 100;
      if (spo2 < 0) spo2 = 0;

      //apply exponential smoothing to spo2 value
      if (spo2Smoothed == 0) spo2Smoothed = spo2;
      else spo2Smoothed = spo2Alpha * spo2 + (1 - spo2Alpha) * spo2Smoothed;

      Serial.println("\nmeasurement complete!");
      Serial.print("average IR="); Serial.println(irAvg, 1);
      Serial.print("average RED="); Serial.println(redAvg, 1);

      if (bpmCount > 0) {
        Serial.print("average BPM (smoothed)="); Serial.println(bpmFiltered, 1);
        Serial.print("smoothed SpO2="); Serial.println(spo2Smoothed, 1);
      } else {
        Serial.println("no beats detected, try adjusting finger pressure or brightness.");
      }

      Serial.println("remove finger to restart measurement.\n");
    }

    delay(SAMPLE_DELAY); //wait a bit before taking next reading
  }
}

