// esp32_SIM_sender_main.cpp
// UPDATED AS OF 3/9/26
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

// ====== BP SIM STATE ======
// 0 = not started (sends -1)
// 1 = reading     (sends -2)
// 2 = done        (sends real value)
static int bpSimState = 0;
static uint32_t bpSimTimer = 0;
static float simSystolic  = 120.0f;
static float simDiastolic = 80.0f;
//

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

  // ====== BP SIM STATE MACHINE ======
  static uint32_t loopStartTime = 0;
  if (loopStartTime == 0) loopStartTime = millis();

  if (bpSimState == 0 && millis() - loopStartTime > 5000) {
    // After 5s -> start reading
    bpSimState = 1;
    bpSimTimer = millis();
    Serial.println("BP SIM: Reading...");
  }
  if (bpSimState == 1 && millis() - bpSimTimer > 15000) {
    // After 15s of reading -> done, pick a random BP value
    bpSimState = 2;
    
    // UPDATE BP VALUE - random normal BP with some noise, clamp to realistic range
    simSystolic  = clampf(115.0f + random(-10, 10), 100, 125);  // normal range
    simDiastolic = clampf(75.0f  + random(-5,  5),  60,  85);   // normal range
    
    Serial.printf("BP SIM: Done! %d/%d mmHg\n", (int)simSystolic, (int)simDiastolic);
  }

  String bpSysStr, bpDiaStr;
  if (bpSimState == 0) {
    bpSysStr = "-1"; bpDiaStr = "-1";  // N/A
  } else if (bpSimState == 1) {
    bpSysStr = "-2"; bpDiaStr = "-2";  // Reading...
  } else {
    bpSysStr = String((int)simSystolic);
    bpDiaStr = String((int)simDiastolic);
  }
  // ==================================

  // Simulated vitals (changing)
  t += 0.18f;

  // Risk 0 ranges: HR 60-100, SpO2 95-100, Temp 36-37.5, Resp 12-20, BP 90-120/60-80
  // Small swings to occasionally hit risk 1
  float HR   = clampf(80.0f + 8.0f  * sinf(t),         60,  105);  // mostly 72-88, occasionally hits 100-105 (risk 1)
  float SpO2 = clampf(97.0f + 1.5f  * sinf(t * 0.55f), 94,  100);  // mostly 96-98, occasionally hits 94-95 (risk 1)
  float Temp = clampf(36.8f + 0.4f  * sinf(t * 0.20f), 36,   38);  // mostly 36.4-37.2, occasionally hits 37.6-38 (risk 1)
  float Resp = clampf(15.0f + 3.0f  * sinf(t * 0.35f), 10,   22);  // mostly 12-18, occasionally hits 21-24 (risk 1)

  // JSON matches your server parser keys exactly:
   // All other vitals always send real values
  String json =
      String("{") +
      "\"timestamp\":\"" + isoTimestampUTC() + "\"," +
      "\"source\":\"******ESP32_WROOM_FREENOVE******\"," +
      "\"device_ip\":\"" + WiFi.localIP().toString() + "\"," +
      "\"seq\":" + String(seq++) + "," +
      "\"HR\":"   + String((int)roundf(HR))   + "," +
      "\"SpO2\":" + String((int)roundf(SpO2)) + "," +
      "\"Temp\":" + String(Temp, 1)           + "," +
      "\"Resp\":" + String((int)roundf(Resp)) + "," +
      "\"BP_sys\":" + bpSysStr + "," +
      "\"BP_dia\":" + bpDiaStr +
      "}";

  HTTPClient http;
  http.begin(INGEST_URL);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST((uint8_t*)json.c_str(), json.length());
  http.end();

  //TO SEE WHAT THE ESP32 IS SENDING, UNCOMMENT THE LINE BELOW
  //Serial.println(json);

  Serial.print("POST code="); Serial.print(code);
  Serial.print("  BP state="); Serial.print(bpSimState);
  Serial.print("  BP="); Serial.println(bpSysStr + "/" + bpDiaStr);
}
