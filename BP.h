#include <Adafruit_MPRLS.h>
#include "Highpass.h"
#include "Lowpass.h"
#include <Arduino.h>

// =================================================================================================
// FOR CIRCUIT DIAGRAM REFER TO: FINAL CIRCUIT DIAGRAM
// =================================================================================================
// Filter declarations
HighPass<1> hp1(0.5, 13, true);
LowPass<1> lp1(5, 13, true);

//=============================================================================
// Task communication 
//=============================================================================
// Flags
bool is_activated = 0;
bool is_pumping = 0;
bool is_releasing = 0;
bool is_reading = 0;
bool bpSensorReady = 0;

// static bool autoRetryDone = false; //ADDED 03/01: for retrying if pressure fails (Unsure what this does for now)

// LLM vitals flag
bool BP_Vitals_Measuring = 0;

// Values
float curr_pressure = 0;
//=============================================================================

// Periods
const unsigned long sample_pressure_PERIOD = 75;
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
const float diastolic_ratio = 0.65; 
//=============================================================================

int tick_sample_pressure(int state) {
  // Create an array for tracking pressure
  switch(state) {
    case sample_pressure_INIT:
      state = sample_pressure_OFF;
      break;
    case sample_pressure_CALIBRATE:
      if (counter_calibrate == baseline_samples) {
        baseline_pressure = baseline_pressure / baseline_samples;
        Serial.print("Baseline Pressure: "); Serial.println(baseline_pressure);

        // Flags
        is_pumping = 1;
        
        state = sample_pressure_ON;
      }
      break;
    case sample_pressure_ON:
      if(is_activated == 0) { // When completed measurement
        for(int i = 0; i <= pa_index; ++i){
          curr_val = pressure_array_HP[i];

          if(curr_val > max_HP){
            max_HP = curr_val;
            max_HP_index = i;
          }
        }
        
        // Debugging: Output all array values onto serial monitor
        for(int i = 0; i <= pa_index; ++i) {
          Serial.print(pressure_array[i]); Serial.print(", ");
        }
        Serial.print("\n");
        for(int i = 0; i <= pa_index; ++i) {
          Serial.print(pressure_array_HP[i]); Serial.print(", ");
        }
        Serial.print("\n");

        // Systolic
        for(int i = 1; i < max_HP_index; ++i) { 
          if((pressure_array[i] < 90.0) || (pressure_array[i] > 130.0)) { // If not within 90-130, go to next iteration
            continue; // Skips remaining code and proceeds to next iteration
          }
          if(abs((pressure_array_HP[i] - (max_HP * systolic_ratio))) < abs((pressure_array_HP[systolic_index] - (max_HP * systolic_ratio)))) { // Choose the closest HP to the sys ratio value
            // Serial.print("Picked "); Serial.print(i); Serial.print(" because ");  Serial.print(abs((pressure_array_HP[i] - (max_HP * systolic_ratio)))); Serial.print(" is less than "); Serial.println(abs((pressure_array_HP[systolic_index] - (max_HP * systolic_ratio))));
            systolic_index = i;
          }
          if(abs((pressure_array_HP[i] - (max_HP * systolic_ratio))) <= 0.05) { // Stop searching for the systolic index if the current index's value is within threshold
            break;
          }
        }
        systolic = pressure_array[systolic_index];

        // Diastolic
        diastolic_index = max_HP_index; // Start from the max high pass pressure reading
        
        for(int i = pa_index; i > max_HP_index; --i) { // Inclusive max_HP_index to inclusive pa_index (pointing to the tail of the pressure arrays);
          if((pressure_array[i] < 60.0) || (pressure_array[i] > 100.0)) { // If not within 60-100, go to next iteration
            continue; // Skips remaining code and proceeds to next iteration
          }
          if(abs(pressure_array_HP[i] - (max_HP * diastolic_ratio)) < abs(pressure_array_HP[diastolic_index] - (max_HP * diastolic_ratio))) { // Choose the closest HP to the dia ratio value 
            diastolic_index = i;
          }
          if(abs((pressure_array_HP[i] - (max_HP * diastolic_ratio))) <= 0.05) break; // Stop searching for the diastolic index if the current index's value is within threshold
        }
        diastolic = pressure_array[diastolic_index];

        // Output all results
        Serial.print("Max Val: "); Serial.println(max_HP);
        Serial.print("Max Index: "); Serial.println(max_HP_index);

        Serial.print("Systolic Val: "); Serial.println(pressure_array_HP[systolic_index]);
        Serial.print("Systolic Index: "); Serial.println(systolic_index);

        Serial.print("Diastolic Val: "); Serial.println(pressure_array_HP[diastolic_index]);
        Serial.print("Diastolic Index: "); Serial.println(diastolic_index);

        Serial.print("Sys: "); Serial.println(systolic);
        Serial.print("Dia: "); Serial.println(diastolic);

        // // Extra redundancy: Checks if the measurement did not go very wrong
        // if(pa_index < 20 || max_HP < 0.3f || systolic_index == 0 || diastolic_index == 0){   //less than 20 samples, 130mmHg reached and max_HP has real values not noise     
        //   Serial.println("==== WARNING: Weak measurement - retry via UI ====");
        //   bpSensorReady = 0;
        //   BP_Vitals_Measuring = 0;
        //   state = sample_pressure_OFF;
        //   break;
        // }
        // autoRetryDone = false;   //ADDED 03/01: reset flag after measurement (Unsure what function this has for now)

        bpSensorReady = 1;
        BP_Vitals_Measuring = 0; // LLM flag - cuff finished, vitals ready!
        
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

        //ADDED 03/01: reinitialize, dont grab value from the end of last run(too low)
        hp1 = HighPass<1>(0.5, 13, true);
        lp1 = LowPass<1>(5, 13, true);

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
      Serial.print("curr_pressure: "); Serial.println(curr_pressure);
      prev_pressure = curr_pressure;
      curr_pressure = (mpr.readPressure() * hPa_to_mmHg) - baseline_pressure;
      delta_pressure = curr_pressure - prev_pressure;
      
      // Spiking loop to cut out spikes
      if((abs(delta_pressure) > 5.0f) && (is_reading == 1)) {
        Serial.print("BP Spike \n");
        break; // Ignores further code, and starts from the top again at the next period
      }

			// Bandpass filter, cascading a low pass filter with cutoff frequency of 5 Hz with a high pass filter with cutoff frequency of 0.5 Hz (0.5 Hz to 5 Hz)
      curr_pressure = lp1.filt(curr_pressure);
      curr_pressure_HP = hp1.filt(curr_pressure);

      // Serial.println(prev_pressure); 
      // Serial.println(curr_pressure);
      // Serial.println(delta_pressure);

      if(curr_pressure >= 160) { // Turns off the pump at 160 mmHg
        is_pumping = 0;
        is_reading = 1;
      }
			
      if(is_reading == 1) {
        // Only take the value if it's within the expected threshold (threshold obtained through experimentation) Currently: [-0.5 to 2]
        if(curr_pressure <= 160){  // Start reading if less than the assigned value (Usually pressure reaches (this value + 20 mmHg))
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
  
      if((curr_pressure <= 60 && is_reading == 1) || (pa_index == (pa_size - 1))) { // Finish measuring if pressure array is full or current pressure is <= 60
        Serial.print("Stopping \n");
        Serial.println(pa_index);
        Serial.println(curr_pressure);

        // Flags
        is_releasing = 1;
        is_activated = 0;
        is_reading = 0; //ADDED 03/01: break here so no more values get pushed, causing last spike
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
