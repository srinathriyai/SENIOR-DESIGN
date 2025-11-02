//Heart Rate Sensor block
//takes heart rate/pulse for ___ seconds and generates ____ samples
//stores average heart rate data as ____ variable (name)

//#include <Arduino.h>
#include <stdio.h> //here for testing, CHANGED platform.ini to framework = espidf  instead of arduino
#include <string.h>
//IF ARDUINO PLUGGED IN CHANGE ^^^^^

//Heart Rate Sensor block
//takes heart rate/pulse readings for a fixed time window and averages multiple samples
//stores average heart rate data as beatAvg variable

//code edited from sparkfun arduino library

#include <Arduino.h>           //basic Arduino functions
#include <Wire.h>              //needed for I2C communication
#include "MAX30105.h"          //SparkFun library for MAX30102/MAX30105 sensor
#include "heartRate.h"         //includes beat detection algorithm

MAX30105 particleSensor;       //create sensor object

const byte RATE_SIZE = 4;      //number of samples used for averaging
byte rates[RATE_SIZE];         //circular buffer to hold heart rate samples
byte rateSpot = 0;             //index for current sample position
long lastBeat = 0;             //stores the time (ms) of the last detected heartbeat

float beatsPerMinute = 0;      //holds instantaneous BPM value
int beatAvg = 0;               //holds averaged BPM value across RATE_SIZE samples

void setup() {
  Serial.begin(115200);                              //start serial communication for debug
  Serial.println("Initializing Heart Rate Sensor...");

  //try to initialize the MAX30102/MAX30105 sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {  
    Serial.println("MAX30102 not found. Check wiring/power.");  
    while (1);                 //stop program if initialization fails
  }

  Serial.println("Place your index finger on the sensor with steady pressure.");

  particleSensor.setup();                      //configure sensor with default settings
  particleSensor.setPulseAmplitudeRed(0x0A);   //enable red LED at low brightness
  particleSensor.setPulseAmplitudeGreen(0x00); //disable green LED (not needed for HR)
}

void loop() {
  long irValue = particleSensor.getIR();       //read IR signal value from sensor

  //check if a heartbeat occurred using the IR signal
  if (checkForBeat(irValue) == true) {         
    long delta = millis() - lastBeat;          //calculate time since last beat
    lastBeat = millis();                       //update last beat time

    beatsPerMinute = 60 / (delta / 1000.0);    //convert time delta to BPM

    //ignore false readings outside human range
    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute;      //store current BPM sample
      rateSpot %= RATE_SIZE;                          //wrap buffer index if needed

      //compute average BPM over last RATE_SIZE samples
      beatAvg = 0;
      for (byte x = 0; x < RATE_SIZE; x++)
        beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }
  }

  //print current readings for debugging
  Serial.print("IR=");
  Serial.print(irValue);
  Serial.print(", BPM=");
  Serial.print(beatsPerMinute);
  Serial.print(", Avg BPM=");
  Serial.print(beatAvg);

  //check if finger is not placed properly on sensor
  if (irValue < 50000)
    Serial.print(" No finger detected");

  Serial.println();

  delay(100);  //delay for staility
}
