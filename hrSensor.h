//Heart Rate + SpO2 Sensor block
//takes heart rate/pulse for 20 seconds and generates 500 samples
//stores average heart rate data as ____avgHearRate____ variable (name)
//stores average SpO2 as ____avgSpO2_____ variable (name)


//code edited+revised from sparkfun arduino library

#include <Wire.h>                //for I2C communication
#include "MAX30105.h"            //SparkFun MAX3010x sensor library
#include "spo2_algorithm.h"      //SparkFun algorithm for HR and SpO2

MAX30105 particleSensor;         //create sensor object

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
//for Arduino Uno (limited SRAM)
uint16_t irBuffer[500];          //IR LED data buffer (16-bit)
uint16_t redBuffer[500];         //RED LED data buffer (16-bit)
#else
//for ESP32 or boards with more memory
uint32_t irBuffer[500];          //IR LED data buffer (32-bit)
uint32_t redBuffer[500];         //RED LED data buffer (32-bit)
#endif

int32_t bufferLength = 500;      //total samples collected (~20 seconds @25Hz)
int32_t spo2;                    //current SpO2 value
int8_t validSPO2;                //SpO2 validity flag
int32_t heartRate;               //current heart rate
int8_t validHeartRate;           //HR validity flag

float avgSpO2 = 0;               //average SpO2 result
float avgHeartRate = 0;          //average heart rate result
float sumSpO2 = 0;               //sum for SpO2 averaging
float sumHeartRate = 0;          //sum for HR averaging
int sampleCount = 0;             //number of valid samples in averaging

byte readLED = 13;               //blinks to show active sampling

void setup() {
  Serial.begin(9600);            //initialize serial output
  pinMode(readLED, OUTPUT);      //set onboard LED pin to output

  //initialize sensor and check connection
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println(F("MAX30102 not found. Check wiring/power."));
    while (1);
  }

  Serial.println(F("Attach sensor to finger firmly. Press any key to start."));
  while (Serial.available() == 0); //wait until user presses key
  Serial.read();                   //clear input

  //configure sensor parameters for accurate readings
  byte ledBrightness = 100;     //LED brightness (0–255)
  byte sampleAverage = 4;       //average samples for smoothing (1–32)
  byte ledMode = 2;             //use Red + IR LEDs
  byte sampleRate = 25;         //25 samples per second (~medical rate)
  int pulseWidth = 411;         //longer pulse = more light
  int adcRange = 4096;          //ADC range for accurate reading

  //apply configuration
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  Serial.println(F("Sensor initialized. Collecting HR and SpO2 data..."));
}

void loop() {
  //collect 500 samples (~20 seconds)
  for (int i = 0; i < bufferLength; i++) {
    while (particleSensor.available() == false) particleSensor.check(); //wait for new sample

    redBuffer[i] = particleSensor.getRed(); //get RED LED value
    irBuffer[i] = particleSensor.getIR();   //get IR LED value
    particleSensor.nextSample();            //move to next sample

    digitalWrite(readLED, !digitalRead(readLED)); //blink LED on each read
  }

  //use SparkFun’s algorithm to calculate HR and SpO2
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer,
                                         &spo2, &validSPO2, &heartRate, &validHeartRate);

  //update averages if valid readings are produced
  if (validSPO2 && validHeartRate) {
    sumSpO2 += spo2;            //add current SpO2 to sum
    sumHeartRate += heartRate;  //add current HR to sum
    sampleCount++;              //increment valid count
  }

  //calculate average HR and SpO2 after full 20s sampling
  if (sampleCount > 0) {
    avgSpO2 = sumSpO2 / sampleCount;         //compute average SpO2
    avgHeartRate = sumHeartRate / sampleCount; //compute average HR
  }

  //print current and average readings to serial
  Serial.print(F("Current SpO2="));
  Serial.print(spo2);
  Serial.print(F("%, HR="));
  Serial.print(heartRate);
  Serial.print(F(" BPM | Avg SpO2="));
  Serial.print(avgSpO2);
  Serial.print(F("%, Avg HR="));
  Serial.println(avgHeartRate);

  //reset sums and repeat every 20 seconds
  sumSpO2 = 0;
  sumHeartRate = 0;
  sampleCount = 0;

  Serial.println(F("20-second cycle complete. Restarting measurement..."));
}
