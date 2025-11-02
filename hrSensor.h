//Heart Rate Sensor block
//takes heart rate/pulse for 20 seconds and generates average value
//stores average heart rate data as ____ variable (name)


//code edited+revised from sparkfun arduino library

//Heart Rate Sensor block
//takes heart rate/pulse readings for SAMPLE_DURATION_MS milliseconds and averages them
//stores average heart rate data as avgBPM variable

#include <Arduino.h>           //basic Arduino functions
#include <Wire.h>              //I2C communication library
#include "MAX30105.h"          //SparkFun library for MAX30102/MAX30105
#include "heartRate.h"         //beat detection algorithm

//===USER CONFIGURATION===//
#define SAMPLE_DURATION_MS 20000  //20 seconds
#define RATE_SIZE 8               //8-beat smoothing window
//=========================//

MAX30105 particleSensor;          //create MAX30105 sensor object

const byte RATE_SIZE = 4;         //number of samples used for smoothing BPM
byte rates[RATE_SIZE];            //buffer to store BPM samples
byte rateSpot = 0;                //current index in sample buffer
long lastBeat = 0;                //timestamp of the last detected beat

float beatsPerMinute = 0;         //instantaneous BPM
float avgBPM = 0;                 //averaged BPM result

void setup() {
  Serial.begin(115200);           //initialize serial communication for debugging
  Serial.println("Initializing Heart Rate Sensor Block...");

  //initialize MAX30102/MAX30105 sensor via I2C
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("Error: MAX30102 not found. Check wiring/power.");
    while (1);                    //halt program if sensor not detected
  }

  //configure sensor LEDs
  particleSensor.setup();                      //default configuration
  particleSensor.setPulseAmplitudeRed(0x0A);   //enable red LED (low power)
  particleSensor.setPulseAmplitudeGreen(0x00); //disable green LED

  Serial.println("Place your finger steadily on the sensor...");
}

void loop() {
  unsigned long startTime = millis();          //mark start time
  float bpmSum = 0;                            //accumulate BPM values
  int sampleCount = 0;                         //track number of valid samples

  Serial.println("Starting heart rate measurement cycle...");

  //collect data until total elapsed time exceeds SAMPLE_DURATION_MS
  while (millis() - startTime < SAMPLE_DURATION_MS) {
    long irValue = particleSensor.getIR();     //read IR value from sensor

    //check if a beat is detected from IR signal
    if (checkForBeat(irValue) == true) {
      long delta = millis() - lastBeat;        //time since last beat
      lastBeat = millis();                     //update beat timestamp

      beatsPerMinute = 60 / (delta / 1000.0);  //convert time delta to BPM

      //validate BPM range for human heart rate
      if (beatsPerMinute < 255 && beatsPerMinute > 20) {
        rates[rateSpot++] = (byte)beatsPerMinute;  //store BPM in buffer
        rateSpot %= RATE_SIZE;                     //wrap buffer index

        //compute running average of BPM samples
        int beatAvg = 0;
        for (byte x = 0; x < RATE_SIZE; x++)
          beatAvg += rates[x];
        beatAvg /= RATE_SIZE;

        bpmSum += beatAvg;                     //add averaged reading to total sum
        sampleCount++;                         //count valid sample
      }
    }

    //optional debug output
    Serial.print("IR="); Serial.print(irValue);
    Serial.print(", BPM="); Serial.print(beatsPerMinute);
    Serial.print(", Samples="); Serial.println(sampleCount);

    delay(SAMPLE_INTERVAL_MS);                 //wait before next read
  }

  //compute final average BPM after sampling period
  if (sampleCount > 0)
    avgBPM = bpmSum / sampleCount;
  else
    avgBPM = 0;                               //no valid beats detected

  //display result for debugging
  Serial.print("Average Heart Rate = ");
  Serial.print(avgBPM);
  Serial.println(" BPM");

  //===Integration Placeholder===//
  //This is where you would send avgBPM to your Bluetooth/cloud API:
  //sendToCloud("heart_rate", avgBPM);
  //=============================//

  delay(10000);                               //wait before next measurement cycle
}
