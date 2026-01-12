//Respiratory Sensor block
//takes analog voltage for _10__ seconds and generates ____ samples
//might need some data parsing
//stores average data as ____ variable (name)

#include <Arduino.h>

//code taken from github repo from instructable and added on to take average and sample over time
//just for confirming stability and testing resistor value needed (10K right now)

const int sensorPin = A0;   //analog input pin for stretch sensor
int sensorValue = 0;        //stores current reading
long totalValue = 0;          //sum for averaging
int sampleCount = 0;          //number of samples taken
unsigned long startTime = 0;   //start time for 10s window

void setup() {
  //initialize serial monitor
  Serial.begin(9600);
  while(!Serial);
  Serial.println("Respiration test starting...");
  delay(1000);           //small delay before measurement
  startTime = millis(); //record start time
}

void loop() {
  //read analog voltage from stretch sensor (0–1023)
  sensorValue = analogRead(sensorPin);
  
  //print raw reading to serial for live monitoring
  Serial.print("Sensor value: ");
  Serial.println(sensorValue);

  //add to running total and increment sample count
  totalValue += sensorValue;
  sampleCount++;

  //after 10 seconds, calculate and print average
  if (millis() - startTime >= 10000) {
    float avgValue = (float)totalValue / sampleCount;
    Serial.println("==================================");
    Serial.print("Average sensor value (10s): ");
    Serial.println(avgValue);
    Serial.println("==================================");

    //reset for next measurement cycle
    totalValue = 0;
    sampleCount = 0;
    startTime = millis();
  }

  // small delay to reduce noise and serial spam
  delay(100);
}
