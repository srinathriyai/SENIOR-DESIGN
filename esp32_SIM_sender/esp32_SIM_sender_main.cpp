// esp32_SIM_sender_main.cpp
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

// ====== EDIT THESE ======
static const char* WIFI_SSID = "HriyaiIP15";
static const char* WIFI_PASS = "Km^uzw@3032";
// ========================

static uint32_t seq = 0;

// PC endpoint (your IP)
static const char* INGEST_URL = "http://172.20.10.8:8000/api/ingest";
static const char* LIVE_URL   = "http://172.20.10.8:8000/api/live";

// 1 packet per second
static const uint32_t SEND_PERIOD_MS = 1000;

// NTP servers for real timestamps (optional)
static const char* NTP1 = "pool.ntp.org";
static const char* NTP2 = "time.nist.gov";

static uint32_t lastSend = 0;
static float t = 0.0f;

static float clampf(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

static String isoTimestampUTC() {
  time_t now = time(nullptr);
  if (now < 1700000000) return "2026-02-10T16:10:00Z";
  struct tm tm_utc;
  gmtime_r(&now, &tm_utc);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
  return String(buf);
}

static void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("WiFi connecting");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    if (millis() - start > 20000) {
      Serial.println("\nWiFi failed -> reboot");
      ESP.restart();
    }
  }
  Serial.println();
  Serial.print("Connected. ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

static void setupTime() {
  configTime(0, 0, NTP1, NTP2);
  Serial.print("Syncing time");
  for (int i = 0; i < 30; i++) {
    if (time(nullptr) > 1700000000) { Serial.println("\nTime synced."); return; }
    delay(250);
    Serial.print(".");
  }
  Serial.println("\nTime not confirmed; using fallback timestamp.");
}

static bool getLiveEnabled() {
  HTTPClient http;
  http.begin(LIVE_URL);
  int code = http.GET();
  String body = http.getString();
  http.end();

  if (code != 200) return false; // fail-safe: stop sending if we can't read state
  return body.indexOf("\"on\":true") >= 0; // expects {"on":true/false}
}


void setup() {
  Serial.begin(115200);
  delay(250);
  randomSeed(esp_random());

  connectWiFi();
  setupTime();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) return;

    // --- Level 2: obey UI Live ON/OFF (poll server once per second) ---
  static uint32_t lastPoll = 0;
  static bool liveEnabled = true;

  if (millis() - lastPoll >= 1000) {
    lastPoll = millis();
    liveEnabled = getLiveEnabled();
    Serial.print("liveEnabled=");
    Serial.println(liveEnabled ? "true" : "false");
  }

  if (!liveEnabled) {
    // Live OFF -> do not send packets
    delay(50); 
    return;
  }

  uint32_t now = millis();
  if (now - lastSend < SEND_PERIOD_MS) return;
  lastSend = now;

  // Simulated vitals (changing)
  t += 0.18f;
  /*
  stable SIM vitals:
   float HR    = 82.0f + 8.0f * sinf(t);
  float SpO2  = 97.0f + 1.2f * sinf(t * 0.55f);
  float Temp  = 36.9f + 0.2f * sinf(t * 0.20f);
  float Resp  = 15.0f + 2.0f * sinf(t * 0.35f);
  float BPsys = 120.0f + 6.0f * sinf(t * 0.28f);
  float BPdia = 78.0f  + 4.0f * sinf(t * 0.31f);
  */
  float HR    = 95.0f  + 55.0f * sinf(t);          // swings 40-150 (hits risk 1,2,3)
  float SpO2  = 94.0f  + 4.0f  * sinf(t * 0.55f);  // swings 90-98 (hits risk 1,2)
  float Temp  = 37.5f  + 1.5f  * sinf(t * 0.20f);  // swings 36-39 (hits risk 1,2)
  float Resp  = 20.0f  + 12.0f * sinf(t * 0.35f);  // swings 8-32 (hits risk 1,2,3)
  float BPsys = 135.0f + 35.0f * sinf(t * 0.28f);  // swings 100-170 (hits risk 1,2,3)
  float BPdia = 88.0f  + 20.0f * sinf(t * 0.31f);  // swings 68-108 (hits risk 1,2,3)
 
  HR    = clampf(HR, 50, 140);
  SpO2  = clampf(SpO2, 85, 100);
  Temp  = clampf(Temp, 34, 41);
  Resp  = clampf(Resp, 6, 35);
  BPsys = clampf(BPsys, 80, 180);
  BPdia = clampf(BPdia, 50, 120);

  // JSON matches your server parser keys exactly:
  String json =
    String("{") +
    "\"timestamp\":\"" + isoTimestampUTC() + "\"," +
    "\"source\":\"******ESP32_WROOM_FREENOVE******\"," +
    "\"device_ip\":\"" + WiFi.localIP().toString() + "\"," +
    "\"seq\":" + String(seq++) + "," +
    "\"HR\":" + String((int)roundf(HR)) + "," +
    "\"SpO2\":" + String((int)roundf(SpO2)) + "," +
    "\"Temp\":" + String(Temp, 1) + "," +
    "\"Resp\":" + String((int)roundf(Resp)) + "," +
    "\"BP_sys\":" + String((int)roundf(BPsys)) + "," +
    "\"BP_dia\":" + String((int)roundf(BPdia)) +
    "}";

  HTTPClient http;
  http.begin(INGEST_URL);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST((uint8_t*)json.c_str(), json.length());
  http.end();

  Serial.print("POST code=");
  Serial.print(code);
  Serial.print("  payload=");
  //TO SEE WHAT THE ESP32 IS SENDING, UNCOMMENT THE LINE BELOW
  //Serial.println(json);
}
