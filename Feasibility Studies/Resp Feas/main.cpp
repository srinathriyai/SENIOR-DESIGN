#include <Arduino.h>

static const uint32_t BAUD = 115200;
static const uint8_t  SENSOR_PIN = A7;

// Period
static const uint16_t SAMPLE_MS = 100;
static unsigned long lastRespSample = 0;
unsigned long now = 0;

// Filtering
static const float FILTER_ALPHA = 0.12f;
float filt = 0.0f;
float prev_filt = 0.0f;

// Signal Storing (ra = respiration array)
const int ra_size = 200;
int ra_index = 0;
float ra_filtered[ra_size];
float ra_raw[ra_size];
bool isEnd = 0;

void RESP_init() {
  while(!Serial)
  Serial.begin(BAUD);
  delay(500);
  Serial.println("Respiration breath test starting...");

  // Initialize filter and baseline using first reading
  int x0 = analogRead(SENSOR_PIN);
  filt = (float)x0;
}

void RESP_update() {
  const int raw = analogRead(SENSOR_PIN);   // Read raw analog sensor value (0–4095)

  // low-pass filter
  prev_filt = filt;
  filt = (1.0f - FILTER_ALPHA) * filt + FILTER_ALPHA * (float)raw;
  const int delta = filt - prev_filt;

  ra_raw[ra_index] = raw;
  ra_filtered[ra_index] = filt;

  Serial.print("Raw: "); Serial.print(raw); Serial.print(" Filtered: "); Serial.println(filt);
  ++ra_index;
}

void setup() {
  RESP_init();
}

void loop() {
  now = millis();
  if(now - lastRespSample >= SAMPLE_MS && ra_index < ra_size) { // Sample until respiration array is full
    lastRespSample = now;
    RESP_update();
  }
  if(ra_index >= ra_size && isEnd == 0) {
    for(int i = 0; i < ra_size; ++i) {
      Serial.print(ra_raw[i]); Serial.print(", ");
    }
    Serial.print("\n");

    for(int i = 0; i < ra_size; ++i) {
      Serial.print(ra_filtered[i]); Serial.print(", ");
    }
    Serial.print("\n");

    isEnd = 1;
  }
}
