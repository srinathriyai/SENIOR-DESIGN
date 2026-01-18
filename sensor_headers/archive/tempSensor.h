//Temperature Sensor block
//takes temperature for 10 seconds and generates 40 samples, then averaged
//stores average data as ____objectAvg_____ variable (name)

//reads temperature data from the MLX90614 infrared sensor using I2C.
//samples data for a fixed duration, averages the samples, stores the result in a variable (avgTempC).
 
//avgTempC sent to cloud API or LLM system for rank setting ?


//Edited from arduino library given example *****
#include <Wire.h>
#include <Adafruit_MLX90614.h>

// create sensor object
Adafruit_MLX90614 mlx = Adafruit_MLX90614();



// define how long to sample (ms) and delay between readings
#define SAMPLE_TIME 15000   // 15 seconds total sampling
#define SAMPLE_DELAY 500    // 0.5 second delay between readings

void setup() {
  Serial.begin(115200);
  while(!Serial);

  Serial.println("mlx90614 temp reader starting...");



  // initialize the sensor
  if (!mlx.begin()) {
    Serial.println("error connecting to mlx sensor, check wiring");
    while (1); // stop if sensor not found
  }
  
  mlx.writeEmissivity(0.98);  // slightly closer to real skin
  Serial.println("sensor initialized, starting readings soon...");
}

void loop() {
  unsigned long startTime = millis(); // record start time
  float ambientSum = 0;               // accumulator for ambient temp
  float objectSum = 0;                // accumulator for object temp
  int sampleCount = 0;                // count how many samples we take

  // loop until 15 seconds have passed
  while (millis() - startTime < SAMPLE_TIME) {
    // read ambient and object temp in c
    float ambientC = mlx.readAmbientTempC();
    float objectC  = mlx.readObjectTempC();

    // convert to fahrenheit
    float ambientF = ambientC * 9.0 / 5.0 + 32;
    float objectF  = objectC * 9.0 / 5.0 + 32;

    // accumulate values for averaging
    ambientSum += ambientF;
    objectSum  += objectF;
    sampleCount++;

    // print each reading
    Serial.print("ambient ="); Serial.print(ambientF); Serial.print("*F\t");
    Serial.print("object ="); Serial.println(objectF); Serial.println("*F");

    delay(SAMPLE_DELAY); // wait 0.5s before next reading
  }

  // compute averages after loop
  float ambientAvg = ambientSum / sampleCount;
  float objectAvg  = objectSum / sampleCount;

  // print average results
  Serial.println("====================================");
  Serial.print("average ambient temp ="); Serial.print(ambientAvg); Serial.println("*F");
  Serial.print("average object temp ="); Serial.print(objectAvg); Serial.println("*F");
  Serial.println("====================================");

  // wait a bit before starting new measurement
  delay(2000);
}
