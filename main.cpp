#include <Arduino.h>
#include "NEWHR_Sensor.h"
#include "NEWTEMP_Sensor.h"
#include "BP.h"
#include "RESP.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
//#include "esp_wpa2.h"

/*
        --------------2/16/26----------------
    Expected Behavior:
    - UI live button ON -> (ESP32 serial) liveEnabled=true & HTTP 200 -> (Server) [ingest] prints -> (UI) values update
    - UI live button OFF -> (ESP32 serial) liveEnabled=false & skipping POST -> (Server) [ingest] stops printing -> (UI) goes N/A 
*/

//WiFi credentials - CHANGE THESE
//regular WiFi:
const char* WIFI_SSID = "iPhone";
const char* WIFI_PASSWORD = "milliondollars";

//PC server address - CHANGE to PC's local IP
const char* PC_SERVER_URL = "http://172.20.10.5:8000/api/ingest";  //IP using ipconfig/ifconfig

const char* PC_LIVE_URL = "http://172.20.10.5:8000/api/live";
const char* PC_LIVE_URL_SENSE = "http://172.20.10.5:8000/api/sense";

//Button tracking for HR/TEMP system
bool lastD6State = HIGH;  // Changed to D6 to avoid GPIO 2 conflict with BP

// BP sensor status
bool PS_check_pass = false;

// LLM data output timing
unsigned long lastLLMOutput = 0;
const unsigned long LLM_OUTPUT_INTERVAL = 1000;  // Send to PC every 5 seconds

void connectWiFi() {
    Serial.print("Connecting to WiFi");
    delay(100);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    //regular WiFi (WPA/WPA2-PSK): change definitions not these
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    //WPA2-Enterprise (uncomment if using campus eduroam):
    // WiFi.disconnect(true);
    // WiFi.mode(WIFI_STA);
    //esp_wifi_sta_wpa2_ent_set_username((uint8_t *)EAP_USERNAME, strlen(EAP_USERNAME));
    //esp_wifi_sta_wpa2_ent_set_password((uint8_t *)EAP_PASSWORD, strlen(EAP_PASSWORD));
    //esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)EAP_IDENTITY, strlen(EAP_IDENTITY));

    //esp_wifi_sta_wpa2_ent_enable();
    //WiFi.begin(WIFI_SSID);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("ESP32 IP: ");
        Serial.println(WiFi.localIP());
        Serial.print("Gateway: ");
        Serial.println(WiFi.gatewayIP());
        Serial.print("Subnet: ");
        Serial.println(WiFi.subnetMask());
    } else {
        Serial.println("\nWiFi connection failed!");
        Serial.println(WiFi.status());
    }
}

bool getLiveEnabled() {
    HTTPClient http;
    http.setTimeout(1500);
    http.begin(PC_LIVE_URL);
    int code = http.GET();
    String body = http.getString();
    http.end();

    if (code != 200) return false;            // fail-safe
    return body.indexOf("\"on\":true") >= 0;  // expects {"on":true/false}
}

bool getSenseEnabled() {
    HTTPClient http;
    http.setTimeout(1500);
    http.begin(PC_LIVE_URL_SENSE);
    int code = http.GET();
    String body = http.getString();
    http.end();
    if (code != 200) return false;            // fail-safe
    return body.indexOf("\"start\":true") >= 0;  // expects {"on":true/false}
}

void setup(){
    Serial.begin(115200);
    delay(2000);
    Serial.println("SETUP START");

    // Connect to WiFi FIRST
    connectWiFi();
    WiFi.setSleep(false);

    //=============================================================================
    // BP SETUP
    //=============================================================================
    
    //initialization
    pinMode(AIN1, OUTPUT); digitalWrite(AIN1, LOW); // RELEASE ON
    pinMode(AIN2, OUTPUT); digitalWrite(AIN2, LOW); // ^
    pinMode(BIN1, OUTPUT); digitalWrite(BIN1, LOW); // MOTOR OFF
    pinMode(BIN2, OUTPUT); digitalWrite(BIN2, LOW); // ^ 

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

    Serial.flush();
    
    // BP sensor initialization (non-blocking)
    if(!mpr.begin()){
        Serial.print("Pressure sensor check failed - BP disabled\n");
        Serial.flush();
        PS_check_pass = false;
    } else{
        Serial.print("Pressure sensor check passed\n");
        Serial.flush();
        PS_check_pass = true;
    }
    //=============================================================================
    
    //initialize HR and TEMP after BP (to avoid I2C conflicts)
    HR_init();
    TEMP_init();
    RESP_init();
    
    Serial.println("\n=== READY ===");
    Serial.println("Press D6 to start HR+TEMP+RESP measurements");
    Serial.println("Press A0 to start BP measurement");
    
}

void loop(){
    //=============================================================================
    // BP TASKS - Run continuously if sensor is ready
    //=============================================================================
    if(PS_check_pass){
        currentmillis = millis();
        for(unsigned int i = 0; i < NUM_TASKS; i++) {
            if(currentmillis - tasks[i].elapsedTime >= tasks[i].period) {
                tasks[i].state = tasks[i].TickFct(tasks[i].state);
                tasks[i].elapsedTime = currentmillis;
            }
        }
    }
    //=============================================================================

    //HR sensor start loop based on button, will be changed to call from LLM or something
    //pinMode(7, INPUT_PULLUP);
    //bool currentD6 = digitalRead(7);   //button press testing
    //UI button press loop, waiting for UI 'start machine'
    static bool lastSenseState = false;
    bool senseNow = getSenseEnabled();
    if(!lastSenseState && senseNow){

        Serial.println("UI triggered - STARTING SENSORS");
        
        is_activated = 1;
        TEMP_startMeasurement();
        HR_startMeasurement();
        RESP_startMeasurement();
    }
    lastSenseState = senseNow;

    //if(lastD6State == HIGH && currentD6 == LOW){
        //is_activated = 1;
    //lastD6State = currentD6;

    //non-blocking
    TEMP_update();
    HR_update();
    RESP_update();

    //  HRIYAI EDITING 2/24/26 - LLM DATA TRANSMISSION 

    //=============================================================================
    //LLM DATA TRANSMISSION - Send to PC web server every 5 seconds
    //=============================================================================
    if(millis() - lastLLMOutput >= LLM_OUTPUT_INTERVAL) 
    {
        lastLLMOutput = millis();
    
        if (WiFi.status() == WL_CONNECTED)
        {
            // --- UI Live ON/OFF gate (poll /api/live once per second) ---
            static uint32_t lastPoll = 0;
            static bool liveEnabled = true;

            if (millis() - lastPoll >= 1000) 
            {
                lastPoll = millis();
                liveEnabled = getLiveEnabled();
                Serial.printf("liveEnabled=%s\n", liveEnabled ? "true" : "false");
            }

            if (!liveEnabled) 
            {
                Serial.println("Live OFF -> skipping POST");
            } 
            else 
            {
                float currentHR   = HR_getMeasurement();
                float currentO2   = O2_getMeasurement();
                float currentTemp = TEMP_getMeasurement();
                float currentResp = RESP_getMeasurement();

                StaticJsonDocument<256> doc;
                doc["HR"]     = (HR_isActive() || currentHR  <= 0) ? -1 : currentHR;
                doc["SpO2"]   = (HR_isActive() || currentO2  <= 0) ? -1 : currentO2;
                doc["Temp"]   = (currentTemp <= 0) ? -1 : currentTemp;
                doc["Resp"]   = (currentResp <= 0) ? -1 : currentResp;
                //doc["BP_sys"] = bpSensorReady ? BP_getSystolic()  : -1;
                //doc["BP_dia"] = bpSensorReady ? BP_getDiastolic() : -1;
                doc["BP_sys"] = bpSensorReady ? BP_getSystolic()  : (BP_Vitals_Measuring ? -2 : -1);
                doc["BP_dia"] = bpSensorReady ? BP_getDiastolic() : (BP_Vitals_Measuring ? -2 : -1);


                static uint32_t seq = 0;
                doc["source"]    = "!!!!!!!THE_REAL_ESP32_SENSOR_MODS!!!!!!!";
                doc["device_ip"] = WiFi.localIP().toString();
                doc["seq"]       = seq++;

                String jsonString;
                serializeJson(doc, jsonString);

                Serial.println("=== DEBUG: JSON TO BE SENT ===");
                Serial.println(jsonString);
                Serial.println("================================");

                HTTPClient http;
                http.setTimeout(3000);
                http.begin(PC_SERVER_URL);
                http.addHeader("Content-Type", "application/json");

                int httpCode = http.POST(jsonString);
                Serial.printf("HTTP %d\n", httpCode);
                
                if (httpCode > 0) 
                {
                    Serial.print("Response body: ");
                    Serial.println(http.getString());
                } 
                else 
                {
                    Serial.print("POST FAILED, error: ");
                    Serial.println(http.errorToString(httpCode));
                }
                http.end();
            }
        }
        else
        {
            static unsigned long lasWifiRetry = 0;
            if (millis() - lasWifiRetry > 20000) {
                lasWifiRetry = millis();
                Serial.println("WiFi disconnected, RECONNECTING...");
                connectWiFi();
            }
        }

    }          
}
