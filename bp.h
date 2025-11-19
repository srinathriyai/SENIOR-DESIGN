//Blood Pressure
//takes blood pressure for ___ seconds and generates ____ samples
//stores average blood pressure data as ____ variable (name)

#include "Adafruit_MPRLS.h"
#include "timerISR.h"
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

//Task struct & SM declarations

// Task communication
unsigned char is_activated = 1;
unsigned char is_pumping = 0;
unsigned char is_releasing = 0;
float curr_pressure = 0;

// Periods
const unsigned long sample_pressure_PERIOD = 500;
const unsigned long air_pump_PERIOD = 1000;
const unsigned long release_valve_PERIOD = 1000;
const unsigned long GCD_PERIOD = 500;

#define NUM_TASKS 3

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
enum sample_pressure{sample_pressure_INIT, sample_pressure_ON, sample_pressure_OFF};
int tick_sample_pressure(int state);

enum air_pump{air_pump_INIT, air_pump_OFF, air_pump_ON};
int tick_air_pump(int state);

enum release_valve{release_valve_INIT, release_valve_OPEN, release_valve_CLOSED};
int tick_release_valve(int state);

void setup() {
  // Initialization (ARDUINO UNO)
  pinMode(in1, OUTPUT); digitalWrite(in1, LOW); // RELEASE ON
  pinMode(in2, OUTPUT); digitalWrite(in2, LOW); // ^
  pinMode(in3, OUTPUT); digitalWrite(in3, LOW); // MOTOR OFF
  pinMode(in4, OUTPUT); digitalWrite(in4, LOW); // ^

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
  // digitalWrite(in1, HIGH); digitalWrite(in2, LOW); // RELEASE OFF
  // delay(2000);

  // //Pump
  // digitalWrite(in3, LOW); digitalWrite(in4, HIGH); // PUMP ON
  // Serial.print("Motor Started");
  // Serial.flush();
  // delay(10000);

  // digitalWrite(in3, LOW); digitalWrite(in4, LOW); // PUMP OFF
  // Serial.print("Motor Stopped");
  // Serial.flush();
  // delay(1000);

  // digitalWrite(in1, LOW); digitalWrite(in2, LOW); // RELEASE ON

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

int tick_sample_pressure(int state) {
  float prev_pressure = 0;
  float delta_pressure = 0;
  float hPa_to_mmHg = 0.75006;
  
  switch(state) {
    case sample_pressure_INIT:
      is_pumping = 1;
      is_releasing = 0;
      state = sample_pressure_ON;
      break;
    case sample_pressure_ON:
      if(is_activated == 0) {
        state = sample_pressure_OFF;
      }
      break;
    case sample_pressure_OFF:
      if(is_activated == 1) {
        state = sample_pressure_INIT;
      }
      break;
  }

  switch(state) {
    case sample_pressure_ON:
      prev_pressure = curr_pressure;
      curr_pressure = (mpr.readPressure() * 0.75006) - 735; // Using a set value for atmopsheric pressure from testing for now
      delta_pressure = curr_pressure - prev_pressure;

      Serial.print(prev_pressure); Serial.flush(); Serial.print("\n"); Serial.flush();
      Serial.print(curr_pressure); Serial.flush(); Serial.print("\n"); Serial.flush();
      Serial.print(delta_pressure); Serial.flush(); Serial.print("\n"); Serial.flush();
      if(curr_pressure >= 170) { // Check if pressure is at 180 mmHg
        is_pumping = 0; // <- *TEMP* INFLATE TO 180 MMHG AND THEN DEFLATE
      }
      if(curr_pressure <= 50 && is_pumping == 0) {
        is_releasing = 1;
      }
      break;
    case sample_pressure_OFF:
      break;
  }

  return state;
}

int tick_release_valve(int state) {
  switch(state) {
    case release_valve_INIT:
      Serial.print("valve init \n");
      digitalWrite(in1, HIGH); digitalWrite(in2, LOW);
      delay(1000);
      state = release_valve_CLOSED;
      break;
    case release_valve_CLOSED:
      if (is_releasing == 1) {
        state = release_valve_OPEN;
      }
      break;
    case release_valve_OPEN:
      if (is_releasing == 0) {
        state = release_valve_INIT;
      }
      break;
  }

  switch(state) {
    case release_valve_CLOSED:
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
      is_pumping = 1;
      state = air_pump_ON;
      break;
    case air_pump_OFF:
      if (is_pumping == 1) {
        state = air_pump_INIT;
      }
      break;
    case air_pump_ON:
      if (is_pumping == 0) {
        state = air_pump_OFF; // Need to raise flag later to repeat
      }
      break;
  }

  switch(state) {
    case air_pump_ON:
      digitalWrite(in3, LOW); digitalWrite(in4, HIGH); // PUMP ON
      break;
    case air_pump_OFF:
      digitalWrite(in3, LOW); digitalWrite(in4, LOW); // PUMP OFF
      Serial.print("Pump off \n");
      break;
  }

  return state;
}
