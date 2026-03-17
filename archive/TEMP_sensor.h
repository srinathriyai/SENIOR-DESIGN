#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>

//initialize temperature sensor
void TEMP_init();
void TEMP_startMeasurement();

void TEMP_update();     //update sampling, called in main.cpp loop()

//check measurement is in progress/workin
bool TEMP_isMeasuring();

//get last averaged temperatures, will change for different output
//debugging purposes
float TEMP_getObjectAvg();
float TEMP_getAmbientAvg();

#endif
