#include "Adafruit_MPRLS.h"
#include "timerISR.h"
#include "Highpass.h"
#include "Lowpass.h"
#include <Arduino.h>

// =================================================================================================
// FOR CIRCUIT DIAGRAM REFER TO: FEASIBILITY STUDY - BP MONITOR CIRCUIT DIAGRAM, DONE ON ARDUINO UNO
// Initialize 2, 3, 4, 5, SCL, and SDA pins

// Activate pump
// Set pin 3 (In3) to high

// Activate solenoid
// Set pin 5 (In1) to high

// Take readings from BP sensor
// Read from pins SCL and SDA using I2C
// =================================================================================================

// Filter declarations
HighPass<1> hp1(1.33, 10, false);

//Task struct & SM declaration

//=============================================================================
// Task communication 
//=============================================================================
// Flags
unsigned char is_activated = 0;
unsigned char is_pumping = 0;
unsigned char is_releasing = 0;
unsigned char is_reading = 0;

// Values
float curr_pressure = 0;
float curr_pressure_HP = 0;
float prev_pressure_HP = 0;

// Counters
int counter_calibrate = 0;
int counter_reading_delay = 0;

// Calibration
float baseline_pressure = 0;
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

// Timer ISR
void TimerISR() {
  TimerFlag = 1;
}

// Adafruit declarations
#define RESET_PIN -1
#define EOC_PIN -1
Adafruit_MPRLS mpr = Adafruit_MPRLS(RESET_PIN, EOC_PIN);

// Pump & valve declarations
int in1 = 5; // *TEMP* ONLY FOR THE ARDUINO UNO CONFIG
int in2 = 4; // ^
int in3 = 3; // ^
int in4 = 2; // ^


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

void setup() {
  // Initialization (ARDUINO UNO)
  pinMode(in1, OUTPUT); digitalWrite(in1, LOW); // RELEASE ON
  pinMode(in2, OUTPUT); digitalWrite(in2, LOW); // ^
  pinMode(in3, OUTPUT); digitalWrite(in3, LOW); // MOTOR OFF
  pinMode(in4, OUTPUT); digitalWrite(in4, LOW); // ^
  // pinMode(A5, INPUT);

  Serial.begin(115200);

  unsigned char i = 0;
  tasks[i].elapsedTime = sample_pressure_PERIOD;
  tasks[i].period = sample_pressure_PERIOD;
  tasks[i].state = sample_pressure_INIT;
  tasks[i].TickFct = &tick_sample_pressure;
  ++i;
  tasks[i].elapsedTime = release_valve_PERIOD;
  tasks[i].period = release_valve_PERIOD;
  tasks[i].state = release_valve_INIT;
  tasks[i].TickFct = &tick_release_valve;
  ++i;
  tasks[i].elapsedTime = air_pump_PERIOD;
  tasks[i].period = air_pump_PERIOD;
  tasks[i].state = air_pump_INIT;
  tasks[i].TickFct = &tick_air_pump;
  ++i;
  tasks[i].elapsedTime = start_button_PERIOD;
  tasks[i].period = start_button_PERIOD;
  tasks[i].state = start_button_INIT;
  tasks[i].TickFct = &tick_start_button;

  Serial.flush();
  
  if (!mpr.begin()) { // Calls mpr.begin() (see Adafruit_MPRLS::begin) and will output a 0 if it could not begin the sensor
    Serial.print("Pressure sensor check failed\n");
    Serial.flush();
    while(1) {
      Serial.print("Stopped, Restart\n");
      Serial.flush();
      delay(1000);
    }
  } else {
    Serial.print("Pressure sensor check passed\n");
    Serial.flush();
  }

  TimerSet(GCD_PERIOD);
  TimerOn();
}

void loop() {
  for (unsigned int i = 0; i < NUM_TASKS; i++) {
    if (tasks[i].elapsedTime >= tasks[i].period) {
      tasks[i].state = tasks[i].TickFct(tasks[i].state); // Runs the tick and then sets the output state to the new state.
      tasks[i].elapsedTime = 0;
    }
    tasks[i].elapsedTime += GCD_PERIOD;
  }
  while(!TimerFlag) {}
  TimerFlag = 0;
}

// Variables for sampling pressure
const int pa_size = 130; // max # of samples to take
int pa_index = 0;
float pressure_array[pa_size];
float pressure_array_HP[pa_size];

int baseline_samples = 3; // # of samples to take for the baseline average
float prev_pressure = 0;
float delta_pressure = 0;
float hPa_to_mmHg = 0.75006;

float curr_val = 0; // Variable for temporarily selecting values
float max_HP = 0;
int max_HP_index = 0;

int systolic_index = 0;
float systolic = 0;
int diastolic_index = 0;
float diastolic = 0;

const float systolic_ratio = 0.85;
const float diastolic_ratio = 0.55;

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
        // Serial.print("Baseline Pressure: "); Serial.println(baseline_pressure);
        is_pumping = 1; // TEMP 0
        state = sample_pressure_ON;
      }
      break;
    case sample_pressure_ON:
      if(is_activated == 0) { // When completed measurement
        for(int i = 0; i <= pa_index; ++i) {
          Serial.print(pressure_array[i]); Serial.print(", ");
        }
        Serial.print("\n");
        for(int i = 0; i <= pa_index; ++i) {
          Serial.print(pressure_array_HP[i]); Serial.print(", ");
        }
        for(int i = 0; i <= pa_index; ++i) {
          curr_val = pressure_array_HP[i];
          if(curr_val > max_HP) {
            max_HP = curr_val;
            max_HP_index = i;
          }
        }

        // Systolic 
        for(int i = 0; i < max_HP_index; ++i) { // Inclusive 0 to exclusive max_HP_index
          if(abs((pressure_array_HP[i] - (max_HP * systolic_ratio))) < abs((pressure_array_HP[systolic_index] - (max_HP * systolic_ratio)))) { // Choose the closest HP to the sys ratio value
            systolic_index = i;
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
        state = sample_pressure_OFF;
        Serial.print("Sys: "); Serial.println(systolic);
        Serial.print("Dia: "); Serial.println(diastolic);
      }
      break;
    case sample_pressure_OFF:
      if(is_activated == 1) {
        is_releasing = 0;
        pa_index = 0; // TEMP 99
        counter_calibrate = 0;
        baseline_pressure = 0;
        is_reading = 0; // TEMP 1
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

      // Serial.print(prev_pressure); Serial.flush(); Serial.print("\n"); Serial.flush();
      // Serial.print(curr_pressure); Serial.flush(); Serial.print("\n"); Serial.flush();
      curr_pressure_HP = hp1.filt(curr_pressure);
      //Serial.print(delta_pressure); Serial.flush(); Serial.print("\n"); Serial.flush();


      if(curr_pressure >= 170) { // Check if pressure is at 180 mmHg
        is_pumping = 0; // <- *TEMP* INFLATE TO 180 MMHG AND THEN DEFLATE
        is_reading = 1;
      }
      if(is_reading == 1) { // For 1 seconds
        ++counter_reading_delay;
      }
      if(is_reading == 1 && (counter_reading_delay >= 2000/sample_pressure_PERIOD)) { // Only start reading after 3 seconds (from experimentation)
        Serial.print("Reading \n");
        if ((curr_pressure_HP - prev_pressure_HP) > 0.05) {
          pressure_array_HP[pa_index] = curr_pressure_HP;
          pressure_array[pa_index] = curr_pressure;
          pa_index = pa_index + 1;
        }
        prev_pressure_HP = curr_pressure_HP;
      }
      if((curr_pressure <= 50 && is_reading == 1) | (pa_index == (pa_size - 1))) { // Finish measuring if pressure array is full or current pressure is <= 50
        Serial.print("Stopping \n");
        Serial.println(pa_index);
        is_releasing = 1;
        is_activated = 0;
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
      Serial.print("NOT RELEASING \n");
      break;
    case release_valve_OPEN:
      digitalWrite(in1, LOW); digitalWrite(in2, LOW); // RELEASING
      Serial.print("RELEASING \n");
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