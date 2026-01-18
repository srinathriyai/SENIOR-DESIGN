#ifndef HR_SENSOR_H
#define HR_SENSOR_H

#include <Arduino.h>

//initialize MAX30102 and button
//port= 0..3 on the PCA9546 mux
void HR_init();


void HR_update();   //called in main.cpp loop()
void HR_startMeasurement();

//check if measurement is active, might be able to remove? Or needed for simultaenous 
bool HR_isActive();

#endif
