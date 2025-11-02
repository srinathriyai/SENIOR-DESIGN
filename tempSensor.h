//Temperature Sensor block
//takes temperature for ___ seconds and generates ____ samples
//stores average data as ____ variable (name)

//reads temperature data from the MLX90614 infrared sensor using I2C.
//samples data for a fixed duration, averages the samples, stores the result in a variable (avgTempC).
 
//avgTempC sent to cloud API or LLM system for rank setting ?


//Edited from arduino library given example *****

#include <Arduino.h>          // Core Arduino functions (setup, loop, Serial, etc.)
#include <Wire.h>             // For I2C communication (MLX90614 uses I2C)
#include <Adafruit_MLX90614.h> // Official Adafruit library for the MLX90614 sensor

// ======== USER CONFIGURATION ======== //
//constants for how long and how often temperature is read
#define SAMPLE_DURATION_MS 5000   //total duration for one data collection cycle (in ms)
#define SAMPLE_INTERVAL_MS 500    //time between each temperature reading (in ms)
// Example: 5000 ms / 500 ms = 10 total samples per cycle


//sensor object named 'mlx' from the Adafruit MLX90614 class
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

//variable storing the averaged temperature result
float avgTempC = 0.0;

// ================== SETUP FUNCTION ================== //
void setup() {
  Serial.begin(115200);     //sart serial communication for debugging at 115200 baud
  while (!Serial);          //(optional wait for debugging or sm)

  Serial.println("Initializing Temperature Sensor Block..."); //startup message for debugging, confirm starting..

  //initialize I2c
  if (!mlx.begin()) {                                                   //mlx.begin() returns false if communication fails
    Serial.println("Error connecting to MLX90614 sensor. Check wiring!");   //error if not workd
    while (1);                             //stop the program here if sensor is not detected
  }

  //read and print the emissivity setting of the sensor
  Serial.print("Emissivity: ");
  Serial.println(mlx.readEmissivity());

  Serial.println("========================================"); // Divider line
}




void loop() {
  //starting time of measurement cycle
  unsigned long startTime = millis();

  //variables for summing and counting temperature readings
  float sumTemp = 0.0;     //accumulates all temperature readings
  int sampleCount = 0;     //counts how many samples have been taken

  Serial.println("Starting temperature data collection...");

  //sample until SAMPLE_DURATION_MS (time between thats set on patient)
  while (millis() - startTime < SAMPLE_DURATION_MS) {
    //read object temperature in Celsius from the sensor
    float temp = mlx.readObjectTempC();

    //tracking the sum
    sumTemp += temp;

    //inc
    sampleCount++;

    //print for debugging atm
    Serial.print("Sample ");
    Serial.print(sampleCount);
    Serial.print(": ");
    Serial.print(temp);
    Serial.println(" °C");

    //wait time between sampling, implement overhaul option from computer??
    delay(SAMPLE_INTERVAL_MS);
  }

  //after data collection, calculate the average temperature
  if (sampleCount > 0) {
    avgTempC = sumTemp / sampleCount;     //avg = total sum/samples
  }

  //print averaged result over serial monitor for testing
  Serial.print("Average Object Temperature = ");
  Serial.print(avgTempC);        //holds average temp value use later for LLM sending
  Serial.println(" °C");

  //
  //integrate sending via bluetooth or cloud API
  //
  
  //wait 10 seconds before starting the next measurement cycle
  delay(10000);
}


