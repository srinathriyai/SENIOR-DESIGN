#ifndef RESP_H
#define RESP_H

#include <Arduino.h>

static const uint32_t BAUD = 115200;
static const uint8_t  SENSOR_PIN = A7;
static bool RESP_measurementStarted = 0;

// Period
static const uint16_t SAMPLE_MS = 100;
static unsigned long lastRespSample = 0;

// Filtering
static const float FILTER_ALPHA = 0.12f; 
float filt = 0.0f;
float prev_filt = 0.0f;

// Debugging
static const uint32_t PRINT_MS = 250;
static unsigned long lastPrint = 0;

// Analysis
static bool isInhale = 0;

int inhale_counter = 0;
const int inhale_counter_threshold = 2;
int exhale_counter = 0;
const int exhale_counter_threshold = 6;  //was 6

unsigned long prevInhale = 0;
unsigned long now = 0;
unsigned long dt = 0;

unsigned long measurementStartTime = 0;

// Buffer (Averages out abnormal readings)
static const int MAX_BUFFER = 6;
uint32_t buffervals[MAX_BUFFER];
int bufferCount = 0;


void pushInterval(uint32_t dt) {
  if (bufferCount < MAX_BUFFER) {
    buffervals[bufferCount++] = dt;
  } else {
    // Shift older intervals left and add newest at end
    for (int i = 1; i < MAX_BUFFER; i++) buffervals[i - 1] = buffervals[i];
    buffervals[MAX_BUFFER - 1] = dt;
  }
}

float computeBPM_median() {
  if (bufferCount == 0) return 0.0f;

  uint32_t tmp[MAX_BUFFER];
  for (int i = 0; i < bufferCount; i++) tmp[i] = buffervals[i];

  // Simple sort (small array → low overhead)
  for (int i = 0; i < bufferCount - 1; i++) {
    for (int j = i + 1; j < bufferCount; j++) {
      if (tmp[j] < tmp[i]) {
        uint32_t t = tmp[i];
        tmp[i] = tmp[j];
        tmp[j] = t;
      }
    }
  }

  uint32_t med = tmp[bufferCount / 2];
  if (med == 0) return 0.0f;
  return 60000.0f / (float)med;
}

void RESP_startMeasurement() {
  Serial.println("RESP_startMeasurement called");  // add this

  RESP_measurementStarted = 1;
  bufferCount = 0;
  isInhale = 0;
  inhale_counter = 0;
  exhale_counter = 0;
}


void RESP_init() {
  Serial.println("Respiration breath test starting...");
  // Initialize filter and baseline using first reading
  int x0 = analogRead(SENSOR_PIN);
  filt = (float)x0;
}

void RESP_update() {
  now = millis();
  //only do detection if measurement started
  // Check if ready to start
  if(RESP_measurementStarted == 0) {
    return;
  }

  // Check if period has elapsed
  if(now - lastRespSample < SAMPLE_MS){
    return;
  }

  int raw = analogRead(SENSOR_PIN);

  //update filter regardless of measurement state
  prev_filt = filt;
  filt = (1.0f - FILTER_ALPHA) * filt + FILTER_ALPHA * (float)raw;
  int delta = filt - prev_filt;
  
  //Serial.printf("R=%d F=%d D=%d\n", raw, (int)lroundf(filt), delta);
  // Serial.print("raw=");
  // Serial.print(raw);
  // Serial.print(" filt=");
  // Serial.print((int)lroundf(filt));
  // Serial.print(" delta=");
  // Serial.println(delta);

  // Inhale logic
  if((delta < 0) && (isInhale == 0)) {
    ++inhale_counter;
  } else {
    inhale_counter = 0;
  }

  if(inhale_counter >= inhale_counter_threshold) {
    now = millis();
    dt = now - prevInhale;
    prevInhale = now;
    isInhale = 1;
    inhale_counter = 0;
    pushInterval(dt);

    // if(dt > 2000 && dt < 15000) pushInterval(dt);   //gating values to be within normal
    Serial.println(computeBPM_median(), 1);
    Serial.print("Inhale detected, dt: ");
    Serial.println(dt);
  }

  // Exhale Logic
  if((delta >= 0) && (isInhale == 1)) {
    ++exhale_counter;
  } else {
    exhale_counter = 0;
  }

  if((exhale_counter >= exhale_counter_threshold)) {
    Serial.print("Exhale detected \n");
    isInhale = 0;
    exhale_counter = 0;
  }
}


float RESP_getMeasurement(){
    return computeBPM_median();
}

#endif

