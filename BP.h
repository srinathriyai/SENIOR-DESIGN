#include "Adafruit_MPRLS.h"
#include "Highpass.h"
#include <Arduino.h>

// =================================================================================================
// FOR CIRCUIT DIAGRAM REFER TO: FINAL CIRCUIT DIAGRAM
// =================================================================================================
// Filter declarations
HighPass<1> hp1(1.33, 10, true);

//=============================================================================
// Task communication 
//=============================================================================
// Flags
bool is_activated = 0;
bool is_pumping = 0;
bool is_releasing = 0;
bool is_reading = 0;
bool bpSensorReady = 0;

// Values
float curr_pressure = 0;
//=============================================================================

// Periods
const unsigned long sample_pressure_PERIOD = 100;
const unsigned long air_pump_PERIOD = 1000;
const unsigned long release_valve_PERIOD = 1000;
const unsigned long start_button_PERIOD = 100;
const unsigned long GCD_PERIOD = 100;

#define NUM_TASKS 4

typedef struct _task{
	signed 	 char state; 		//Task's current state
	unsigned long period; 		//Task period
	unsigned long elapsedTime; 	//Time elapsed since last task tick
	int (*TickFct)(int); 		//Task tick function
} task;

task tasks[NUM_TASKS]; // declared task array with 5 tasks

// "Task manager" timer
unsigned long currentmillis = 0;

// Adafruit declarations
#define RESET_PIN -1
#define EOC_PIN -1
Adafruit_MPRLS mpr = Adafruit_MPRLS(RESET_PIN, EOC_PIN);

// Pump & valve pin declarations
int in1 = 5;
int in2 = 4;
int in3 = 3;
int in4 = 2;

// State enumerations
enum sample_pressure{sample_pressure_INIT, sample_pressure_CALIBRATE, sample_pressure_ON, sample_pressure_OFF};
int tick_sample_pressure(int state);

enum air_pump{air_pump_INIT, air_pump_OFF, air_pump_ON};
int tick_air_pump(int state);

enum release_valve{release_valve_INIT, release_valve_OPEN, release_valve_CLOSED};
int tick_release_valve(int state);

// Replace with check for start signal in full implementation
enum start_button{start_button_INIT, start_button_ON};
int tick_start_button(int state);

//=============================================================================
// Variables for sampling pressure
//=============================================================================
// Counters
int counter_calibrate = 0;
int counter_reading_delay = 0;

// Calibration
float baseline_pressure = 0;
int baseline_samples = 3; // # of samples to take for the baseline average

// Pressure Arrays
const int pa_size = 1200; // max # of samples to take
int pa_index = 0;
float pressure_array[pa_size];
float pressure_array_HP[pa_size];

// Values
float prev_pressure = 0;
float curr_pressure_HP = 0;

// Analysis
const float hPa_to_mmHg = 0.75006;
float delta_pressure = 0;
float curr_val = 0; // Variable for temporarily selecting values
float max_HP = 0;
int max_HP_index = 0;
int systolic_index = 0;
float systolic = 0;
int diastolic_index = 0;
float diastolic = 0;
const float systolic_ratio = 0.55;
const float diastolic_ratio = 0.85;
//=============================================================================

int tick_sample_pressure(int state) {
  // Create an array for tracking pressure
  switch(state) {
    case sample_pressure_INIT:
      pa_index = 0;
      counter_calibrate = 0;
      baseline_pressure = 0;
      is_reading = 0;
      state = sample_pressure_OFF;
      break;
    case sample_pressure_CALIBRATE:
      if (counter_calibrate == baseline_samples) {
        baseline_pressure = baseline_pressure / baseline_samples;
        Serial.print("Baseline Pressure: "); Serial.println(baseline_pressure);
        is_pumping = 1; // TEMP 0
        state = sample_pressure_ON;
      }
      break;
    case sample_pressure_ON:
      if(is_activated == 0) { // When completed measurement
        // for(int i = 0; i <= pa_index; ++i) {
        //   Serial.print(pressure_array[i]); Serial.print(", ");
        // }
        // Serial.print("\n");
        // for(int i = 0; i <= pa_index; ++i) {
        //   Serial.print(pressure_array_HP[i]); Serial.print(", ");
        // }
        // for(int i = 0; i <= pa_index; ++i) {
        //   curr_val = pressure_array_HP[i];
        //   if(curr_val > max_HP) {
        //     max_HP = curr_val;
        //     max_HP_index = i;
        //   }
        // }
        // Serial.print("\n");

        // Systolic 
        for(int i = 0; i < max_HP_index; ++i) { // Inclusive 0 to exclusive max_HP_index
          if(abs((pressure_array_HP[i] - (max_HP * systolic_ratio))) < abs((pressure_array_HP[systolic_index] - (max_HP * systolic_ratio)))) { // Choose the closest HP to the sys ratio value
            // Serial.print("Picked "); Serial.print(i); Serial.print(" because ");  Serial.print(abs((pressure_array_HP[i] - (max_HP * systolic_ratio)))); Serial.print(" is less than "); Serial.println(abs((pressure_array_HP[systolic_index] - (max_HP * systolic_ratio))));
            systolic_index = i;
          }

          // Changes obtained from experimentation:
          if(abs((pressure_array_HP[i] - (max_HP * systolic_ratio))) <= 0.05) { // Stop searching for the systolic index if the current index's value is within threshold
            break;
          }
        }
        systolic = pressure_array[systolic_index];

        // Diastolic
        diastolic_index = max_HP_index; // Start from the max high pass pressure reading
        for(int i = max_HP_index; i <= pa_index; ++i) { // Inclusive max_HP_index to inclusive pa_index (pointing to the tail of the pressure arrays);
          if(abs(pressure_array_HP[i] - (max_HP * diastolic_ratio)) < abs(pressure_array_HP[diastolic_index] - (max_HP * diastolic_ratio))) { // Choose the closest HP to the dia ratio value 
            diastolic_index = i;
          }
        }
        diastolic = pressure_array[diastolic_index];

        Serial.print("Max Val: "); Serial.println(max_HP);
        Serial.print("Max Index: "); Serial.println(max_HP_index);

        Serial.print("Systolic Val: "); Serial.println(pressure_array_HP[systolic_index]);
        Serial.print("Systolic Index: "); Serial.println(systolic_index);

        Serial.print("Diastolic Val: "); Serial.println(pressure_array_HP[diastolic_index]);
        Serial.print("Diastolic Index: "); Serial.println(diastolic_index);

        Serial.print("Sys: "); Serial.println(systolic);
        Serial.print("Dia: "); Serial.println(diastolic);

        bpSensorReady = 1;
        state = sample_pressure_OFF;
      }
      break;
    case sample_pressure_OFF:
      if(is_activated == 1) {
        // Flags
        is_releasing = 0;
        is_reading = 0;

        // Counters
        counter_calibrate = 0;

        // Analysis
        pa_index = 0;
        baseline_pressure = 0;
        systolic_index = 0;
        diastolic_index = 0;

        state = sample_pressure_CALIBRATE;
      }
      break;
  }

  switch(state) {
    case sample_pressure_CALIBRATE:
      curr_pressure = mpr.readPressure() * hPa_to_mmHg;
      baseline_pressure = baseline_pressure + curr_pressure;
      ++counter_calibrate;
      break;
    case sample_pressure_ON:
      prev_pressure = curr_pressure;
      curr_pressure = (mpr.readPressure() * hPa_to_mmHg) - baseline_pressure; // Using a set value for atmopsheric pressure from testing for now
      delta_pressure = curr_pressure - prev_pressure;

      // Serial.println(prev_pressure); 
      // Serial.println(curr_pressure);
      //Serial.println(delta_pressure);
      // curr_pressure_HP = hp1.filt(curr_pressure);


      if(curr_pressure >= 180) { // Check if pressure is at 180 mmHg
        is_pumping = 0; // <- *TEMP* INFLATE TO 180 MMHG AND THEN DEFLATE
        is_reading = 1;
      }
      if(is_reading == 1) { // For 1 seconds
        ++counter_reading_delay;
      }
      if(is_reading == 1) {
        // Serial.print("Reading \n");
        // Changes obtained from experimentation:
        // Only take the value if it's within the expected threshold (threshold obtained through experimentation) Currently: [-0.25 to 2]
        if(curr_pressure_HP < 0) {
          if(curr_pressure_HP > -0.25){
            curr_pressure_HP = curr_pressure_HP * -1;
            pressure_array_HP[pa_index] = curr_pressure_HP;
            pressure_array[pa_index] = curr_pressure;
            pa_index = pa_index + 1;
          }
        } else {
          if(curr_pressure_HP < 2) {
            pressure_array_HP[pa_index] = curr_pressure_HP;
            pressure_array[pa_index] = curr_pressure;
            pa_index = pa_index + 1;
          }
        }
      }
      if((curr_pressure <= 60 && is_reading == 1) | (pa_index == (pa_size - 1))) { // Finish measuring if pressure array is full or current pressure is <= 60
        // Serial.print("Stopping \n");
        // Serial.println(pa_index);
        is_releasing = 1;
        is_activated = 0;
      }
      break;
    case sample_pressure_OFF:
      // Serial.print("NOT SAMPLING ");
      break;
  }

  return state;
}

int tick_release_valve(int state) {
  switch(state) {
    case release_valve_INIT:
      Serial.print("valve init \n");
      digitalWrite(in1, LOW); digitalWrite(in2, LOW);
      is_releasing = 1;
      state = release_valve_OPEN;
      break;
    case release_valve_CLOSED:
      if (is_releasing == 1) {
        state = release_valve_OPEN;
      }
      break;
    case release_valve_OPEN:
      if (is_releasing == 0) {
        state = release_valve_CLOSED;
      }
      break;
  }

  switch(state) {
    case release_valve_CLOSED:
      digitalWrite(in1, HIGH); digitalWrite(in2, LOW);
      // Serial.print("NOT RELEASING \n");
      break;
    case release_valve_OPEN:
      digitalWrite(in1, LOW); digitalWrite(in2, LOW); // RELEASING
      // Serial.print("RELEASING \n");
      break;
  }

  return state;
}

int tick_air_pump(int state) {
  switch(state) {
    case air_pump_INIT:
      Serial.print("pump init \n");
      digitalWrite(in3, LOW); digitalWrite(in4, LOW); // PUMP OFF (DEFAULT STATE)
      is_pumping = 0;
      state = air_pump_OFF;
      break;
    case air_pump_OFF:
      if (is_pumping == 1) {
        // digitalWrite(in3, HIGH); digitalWrite(in4, LOW);
        // delay(1000);
        state = air_pump_ON;
      }
      break;
    case air_pump_ON:
      if (is_pumping == 0) {
        state = air_pump_OFF;
      }
      break;
  }

  switch(state) {
    case air_pump_ON:
      digitalWrite(in3, HIGH); digitalWrite(in4, LOW); // PUMP ON
      // Serial.print("Pump on \n");
      // Serial.flush();
      break;
    case air_pump_OFF:
      digitalWrite(in3, LOW); digitalWrite(in4, LOW); // PUMP OFF
      // Serial.print("Pump off \n");
      // Serial.flush();
      break;
  }

  return state;
}

int tick_start_button(int state) {
  switch(state) {
    case start_button_INIT:
      state = start_button_ON;
      delay(1000);
      break;
    case start_button_ON:
      if (analogRead(A0) > 1000) {
        is_activated = 1;
        // Serial.println(analogRead(A0));
      }
      break;
  }

  switch(state) {
    case start_button_ON:
      break;
  }

  return state;
}

float BP_getSystolic(){
    return systolic;
}
float BP_getDiastolic(){
    return diastolic;
}
