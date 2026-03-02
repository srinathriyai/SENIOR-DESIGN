#include <Adafruit_MPRLS.h>
#include "Highpass.h"
#include <Arduino.h>

// =================================================================================================
// FOR CIRCUIT DIAGRAM REFER TO: FINAL CIRCUIT DIAGRAM
// =================================================================================================
// Filter declarations
HighPass<1> hp1(0.5, 10, true);

//=============================================================================
// Task communication 
//=============================================================================
// Flags
bool is_activated = 0;
bool is_pumping = 0;
bool is_releasing = 0;
bool is_reading = 0;
bool bpSensorReady = 0;

static bool autoRetryDone = false; //ADDED 03/01: for retrying if pressure fails

// LLM vitals flag
bool BP_Vitals_Measuring = 0;

// Values
float curr_pressure = 0;
//=============================================================================

// Periods
const unsigned long sample_pressure_PERIOD = 75; //changed from 100
const unsigned long air_pump_PERIOD = 100;
const unsigned long release_valve_PERIOD = 1000;
const unsigned long start_button_PERIOD = 100;
const unsigned long GCD_PERIOD = 100;

#define NUM_TASKS 3

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
int BIN1 = 5; // Pump
int BIN2 = 4;
int AIN1 = 3; // Valve
int AIN2 = 2;

// State enumerations
enum sample_pressure{sample_pressure_INIT, sample_pressure_CALIBRATE, sample_pressure_ON, sample_pressure_OFF};
int tick_sample_pressure(int state);

enum air_pump{air_pump_INIT, air_pump_OFF, air_pump_ON};
int tick_air_pump(int state);

enum release_valve{release_valve_INIT, release_valve_OPEN, release_valve_CLOSED};
int tick_release_valve(int state);

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
const int pa_size = 1600; // max # of samples to take INCREASED 03/01: to 1600
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
const float diastolic_ratio = 0.75;  //decreased to 0.75 since taking values only shortly after systolic 
//=============================================================================

int tick_sample_pressure(int state) { // Handles reading of the pressure in the BP cuff and calculation of SYS and DIA
  switch(state) {
    case sample_pressure_INIT:
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
        // calculate maxHP prior to guard loop to determine if noisy value or valid
        for(int i = 0; i <= pa_index; ++i){
          curr_val = pressure_array_HP[i];

          if(curr_val > max_HP){
            max_HP = curr_val;
            max_HP_index = i;
          }
        }

        //ADDED 03/01: guard if pressure doesnt get up to 160, just stop and have to press again
        if(pa_index < 20 || max_HP < 0.3f){   //less than 20 samples, 160mmHg reached and max_HP has real values not noise
          Serial.println("==== WARNING: Weak measurement - retry via UI ====");
          bpSensorReady = 0;
          BP_Vitals_Measuring = 0;
          state = sample_pressure_OFF;
          break;
        }

		// DEBUGGING: Dump all numbers in both pressure arrays to the serial monitor
        for(int i = 0; i <= pa_index; ++i) {
          Serial.print(pressure_array[i]); Serial.print(", ");
        }
        Serial.print("\n");
        for(int i = 0; i <= pa_index; ++i) {
          Serial.print(pressure_array_HP[i]); Serial.print(", ");
        }
        Serial.print("\n");


		// Systolic
        bool systolicFound = false; //ADDED
        // Systolic UPDATED 03/01: made index start at 1 since at 0 flags as false 
        for(int i = 1; i < max_HP_index; ++i) { //inclusive 1 to exclusive max_HP_index
          if(pressure_array[i] < 90.0 || pressure_array[i] > 160.0) continue; //ADDED 03/01: only consider within range 90-160

          if(abs((pressure_array_HP[i] - (max_HP * systolic_ratio))) < abs((pressure_array_HP[systolic_index] - (max_HP * systolic_ratio)))) { // Choose the closest HP to the sys ratio value
            // Serial.print("Picked "); Serial.print(i); Serial.print(" because ");  Serial.print(abs((pressure_array_HP[i] - (max_HP * systolic_ratio)))); Serial.print(" is less than "); Serial.println(abs((pressure_array_HP[systolic_index] - (max_HP * systolic_ratio))));
            systolic_index = i;
            systolicFound = true;
          }
          if(abs((pressure_array_HP[i] - (max_HP * systolic_ratio))) <= 0.05) { // Stop searching for the systolic index if the current index's value is within threshold
            break;
          }
        }
		  
        if(!systolicFound) { // Search for systolic outside of range 90-100 mmHg
          Serial.println("WARNING: Systolic window empty, using unconstrained search");
          for(int j = 1; j < max_HP_index; ++j) {
            if(abs((pressure_array_HP[j] - (max_HP * systolic_ratio))) < abs((pressure_array_HP[systolic_index] - (max_HP * systolic_ratio)))) {
              systolic_index = j;
            }
            if(abs((pressure_array_HP[j] - (max_HP * systolic_ratio))) <= 0.05) break;
          }
        }
        systolic = pressure_array[systolic_index];

        // Diastolic
        diastolic_index = max_HP_index; // Start from the max high pass pressure reading
        bool diastolicFound = false; 
        
        for(int i = max_HP_index; i <= pa_index; ++i) { // Inclusive max_HP_index to inclusive pa_index (pointing to the tail of the pressure arrays);
          if(pressure_array[i] < 60.0 || pressure_array[i] > 100.0) continue; //ADDED 03/01: only consider within range 60-100
          
          if(abs(pressure_array_HP[i] - (max_HP * diastolic_ratio)) < abs(pressure_array_HP[diastolic_index] - (max_HP * diastolic_ratio))) { // Choose the closest HP to the dia ratio value 
            diastolic_index = i;
            diastolicFound = true;
          }
		  if(abs((pressure_array_HP[i] - (max_HP * diastolic_ratio))) <= 0.05) { // Stop searching for the diastolic index if the current index's value is within threshold
            break;
          }
        }
        if(!diastolicFound) { // Search for diastolic outside of range 60-100 mmHg
          Serial.println("WARNING: Diastolic window empty, using unconstrained search");
          for(int i = max_HP_index; i <= pa_index; ++i) {
            if(abs(pressure_array_HP[i] - (max_HP * diastolic_ratio)) < abs(pressure_array_HP[diastolic_index] - (max_HP * diastolic_ratio))) {
              diastolic_index = i;
            }
		  	if(abs((pressure_array_HP[i] - (max_HP * diastolic_ratio))) <= 0.05) { // Stop searching for the diastolic index if the current index's value is within threshold
              break;
          	}
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

		// Flags
        bpSensorReady = 1;
        BP_Vitals_Measuring = 0; // LLM flag - cuff finished, vitals ready!
		autoRetryDone = false;   //ADDED 03/01: reset flag after measurement
        
        state = sample_pressure_OFF;
      }
      break;
    case sample_pressure_OFF:
      if(is_activated == 1) {
        // Flags
        is_releasing = 0;
        is_reading = 0;
		bpSensorReady = 0;
        BP_Vitals_Measuring = 1; // LLM flag - cuff started, now reading vitals...

        // Counters
        counter_calibrate = 0;

        // Analysis
        pa_index = 0;
        baseline_pressure = 0;
        systolic_index = 0;
        diastolic_index = 0;
        max_HP = 0;
        max_HP_index = 0;
        systolic = 0;
        diastolic = 0;
        curr_pressure_HP = 0;
        prev_pressure = 0;

        //Reinitialize, dont grab value from the end of last run(too low)
        hp1 = HighPass<1>(0.5,10, true); 
	
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
      
      //ADDED 03/01: spiking loop to cut out spikes
      bool isSpiking = (pa_index > 0 && curr_pressure > pressure_array[pa_index - 1] + 5.0f);
      if(!isSpiking){
        curr_pressure_HP = hp1.filt(curr_pressure);  //filter if not a spike
      }

	  // // DEBUGGING: Show current pressure, previous pressure, and change in pressure on serial monitor
      // Serial.println(prev_pressure); 
      // Serial.println(curr_pressure);
	  // delta_pressure = curr_pressure - prev_pressure;
      // Serial.println(delta_pressure);

      if(curr_pressure >= 160) { // Check if pressure is at 160 mmHg
        is_pumping = 0;
        is_reading = 1;
      }

      if(is_reading == 1) {
        // Serial.print("READING");
        // Only take the value if it's within the expected threshold (threshold obtained through experimentation) Currently: [-0.5 to 2]
        if(curr_pressure <= 160){  // Start reading when pressure has reached 160 mmHg
          if(!isSpiking){ // If a spike in cuff pressure was detected, do not store anything
            if(curr_pressure_HP < 0) { // Negative Cases
              if(curr_pressure_HP > -0.5){
                curr_pressure_HP = curr_pressure_HP * -1;
                pressure_array_HP[pa_index] = curr_pressure_HP;
                pressure_array[pa_index] = curr_pressure;
                pa_index = pa_index + 1;
              }
            } else { // Positive Cases
              if (curr_pressure >= 140) {
                if (curr_pressure_HP < 1) {
                  pressure_array_HP[pa_index] = curr_pressure_HP;
                  pressure_array[pa_index] = curr_pressure;
                  pa_index = pa_index + 1;
                }
              } else if(curr_pressure <= 75) {
                if (curr_pressure_HP < 1.25) {
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
          }
        }
      }

      if((curr_pressure <= 60 && is_reading == 1) || (pa_index == (pa_size - 1))) { // Finish measuring if pressure array is full or current pressure is <= 60
		// // DEBUGGING: Notifies when the measurement has ended and gives the size of the pressure arrays
        // Serial.print("Stopping \n");
        // Serial.println(pa_index);

		// Flags
        is_releasing = 1;
        is_activated = 0;
        is_reading = 0;
      }
      break;
    case sample_pressure_OFF:
      //Serial.print("NOT SAMPLING ");
      break;
  }

  return state;
}

int tick_release_valve(int state) {
  switch(state) {
    case release_valve_INIT:
      digitalWrite(AIN1, LOW); digitalWrite(AIN2, LOW);
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
      digitalWrite(AIN1, HIGH); digitalWrite(AIN2, LOW);
      // Serial.print("NOT RELEASING \n");
      break;
    case release_valve_OPEN:
      digitalWrite(AIN1, LOW); digitalWrite(AIN2, LOW); // RELEASING
      // Serial.print("RELEASING \n");
      break;
  }

  return state;
}

int tick_air_pump(int state) {
  switch(state) {
    case air_pump_INIT:
      digitalWrite(BIN1, LOW); digitalWrite(BIN2, LOW); // PUMP OFF (DEFAULT STATE)
      is_pumping = 0;
      state = air_pump_OFF;
      break;
    case air_pump_OFF:
      if (is_pumping == 1) {
        digitalWrite(BIN1, HIGH); digitalWrite(BIN2, LOW);
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
      digitalWrite(BIN1, HIGH); digitalWrite(BIN2, LOW); // PUMP ON
      //Serial.print("Pump on \n");
      Serial.flush();
      break;
    case air_pump_OFF:
      digitalWrite(BIN1, LOW); digitalWrite(BIN2, LOW); // PUMP OFF
      //Serial.print("Pump off \n");
      Serial.flush();
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


