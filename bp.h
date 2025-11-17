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
const unsigned long sample_pressure_PERIOD = 1000;
const unsigned long air_pump_PERIOD = 1000;
const unsigned long release_valve_PERIOD = 1000;
const unsigned long GCD_PERIOD = 1000;

#define NUM_TASKS 3

typedef struct _task{
	signed 	 char state; 		//Task's current state
	unsigned long period; 		//Task period
	unsigned long elapsedTime; 	//Time elapsed since last task tick
	int (*TickFct)(int); 		//Task tick function
} task;

task tasks[NUM_TASKS]; // declared task array with 5 tasks


// Adafruit declarations
#define RESET_PIN -1
#define EOC_PIN -1
Adafruit_MPRLS mpr = Adafruit_MPRLS(RESET_PIN, EOC_PIN);

// State enumerations
enum sample_pressure{sample_pressure_INIT, sample_pressure_ON};
int tick_sample_pressure(int state);

enum air_pump{air_pump_INIT, air_pump_OFF, air_pump_ON};
int tick_air_pump(int state);

enum release_valve{release_valve_INIT, release_valve_OPEN, release_valve_CLOSED};
int tick_release_valve(int state);

// Timer ISR
void TimerISR() {
	for ( unsigned int i = 0; i < NUM_TASKS; i++ ) {                   // Iterate through each task in the task array
		if ( tasks[i].elapsedTime == tasks[i].period ) {           // Check if the task is ready to tick
			tasks[i].state = tasks[i].TickFct(tasks[i].state); // Tick and set the next state for this task
			tasks[i].elapsedTime = 0;                          // Reset the elapsed time for the next tick
		}
		tasks[i].elapsedTime += GCD_PERIOD;                        // Increment the elapsed time by GCD_PERIOD
	}
}

 // Doing in main without states for now to test basic interaction
void setup() {
  // Initialization (ARDUINO UNO)
  int in1 = 5; pinMode(in1, OUTPUT); digitalWrite(in1, LOW); // RELEASE ON
  int in2 = 4; pinMode(in2, OUTPUT); digitalWrite(in2, LOW); // ^
  int in3 = 3; pinMode(in3, OUTPUT); digitalWrite(in3, LOW); // MOTOR OFF
  int in4 = 2; pinMode(in4, OUTPUT); digitalWrite(in4, LOW); // ^

  Serial.begin(115200);

  unsigned char i = 0;
  tasks[i].elapsedTime = sample_pressure_PERIOD;
  tasks[i].period = sample_pressure_PERIOD;
  tasks[i].state = sample_pressure_INIT;
  tasks[i].TickFct = &tick_sample_pressure;
  ++i;
  tasks[i].elapsedTime = air_pump_PERIOD;
  tasks[i].period = air_pump_PERIOD;
  tasks[i].state = air_pump_INIT;
  tasks[i].TickFct = &tick_air_pump;
  ++i;
  tasks[i].elapsedTime = release_valve_PERIOD;
  tasks[i].period = release_valve_PERIOD;
  tasks[i].state = release_valve_INIT;
  tasks[i].TickFct = &tick_release_valve;
    
  TimerSet(GCD_PERIOD);
  TimerOn();

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
}

int tick_sample_pressure(int state) {
  return state;
}

int tick_air_pump(int state) {
  return state;
}

int tick_release_valve(int state) {
  return state;
}
