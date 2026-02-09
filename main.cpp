#include <Arduino.h>
#include "HR_Sensor.h"
#include "TEMP_Sensor.h"
#include "BP.h"
#include <WiFi.h>
#include <HTTPClient.h>
//#include "esp_wpa2.h"
//#include <ArduinoJson.h>

//WiFi credentials - CHANGE THESE
//regular WiFi:
const char* WIFI_SSID = "iPhone";
const char* WIFI_PASSWORD = "milliondollars";

//WPA2-Enterprise  eduroam config:
//uncomment
//const char* EAP_IDENTITY = "apapi001@ucr.edu";
//const char* EAP_USERNAME = "apapi001@ucr.edu";  
//const char* EAP_PASSWORD = "password";

//PC server address - CHANGE to PC's local IP
const char* PC_SERVER_URL = "http://172.20.10.5:8000/api/ingest";  //IP using ipconfig/ifconfig

//Button tracking for HR/TEMP system
bool lastD6State = HIGH;  // Changed to D6 to avoid GPIO 2 conflict with BP

// BP sensor status
bool PS_check_pass = false;

// LLM data output timing
unsigned long lastLLMOutput = 0;
const unsigned long LLM_OUTPUT_INTERVAL = 5000;  // Send to PC every 5 seconds

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
    pinMode(in1, OUTPUT); digitalWrite(in1, LOW); // RELEASE ON
    pinMode(in2, OUTPUT); digitalWrite(in2, LOW); // ^
    pinMode(in3, OUTPUT); digitalWrite(in3, LOW); // MOTOR OFF
    pinMode(in4, OUTPUT); digitalWrite(in4, LOW); // ^ 

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
    
    Serial.println("\n=== READY ===");
    Serial.println("Press D6 to start HR+TEMP measurements");
    Serial.println("OR press D4 for HR only");
    Serial.println("BP uses A0 button (from BP.h code)");
    Serial.println("\nNOTE: D2 is used by BP system (in4), don't use for buttons!");
    
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
    bool currentD6 = digitalRead(7);
    if(lastD6State == HIGH && currentD6 == LOW){
        delay(50);
        
        Serial.println("\n======================================");
        Serial.println("D6 PRESSED - STARTING HR+TEMP SENSORS");
        Serial.println("======================================");
        
        TEMP_startMeasurement();

        HR_startMeasurement();
    }
    lastD6State = currentD6;
    
    // Update HR and TEMP sensors (non-blocking)
    TEMP_update();
    HR_update();


    //=============================================================================
    //LLM DATA TRANSMISSION - Send to PC web server every 5 seconds
    //=============================================================================
    if(millis() - lastLLMOutput >= LLM_OUTPUT_INTERVAL){
        lastLLMOutput = millis();
    
        //HR measurement not working while wifi is trying to send to LLM, will need to workaround?
        if(HR_isActive()){
            Serial.println("LLM send skipped - HR sampling active");
        }
        else{
            //if WiFi is connected, send data
            if (WiFi.status() == WL_CONNECTED) {
    
                float currentHR   = HR_getMeasurement();
                float currentO2   = O2_getMeasurement();
                float currentTemp = TEMP_getMeasurement();
    
                StaticJsonDocument<256> doc;
                doc["HR"]    = currentHR;
                doc["SpO2"]  = currentO2;
                doc["Temp"]  = currentTemp;
                doc["Resp"]  = 0;   // placeholder
                doc["BP_sys"] = 0;//systolic; //placeholder
                doc["BP_dia"] = 0;//diastolic; //placeholder
    
                String jsonString;
                serializeJson(doc, jsonString);
    
                 // ----- DEBUG PRINT -----
                Serial.println("=== DEBUG: JSON TO BE SENT ===");
                Serial.println(jsonString);
                Serial.println("================================");
    
                HTTPClient http;
                http.setTimeout(3000);
                http.begin(PC_SERVER_URL);
                http.addHeader("Content-Type", "application/json");
    
                int httpCode = http.POST(jsonString);
    
                // ADDED THE FOLLOWING DEBUG PRINTS TO CHECK HTTP RESPONSE
                //http.getString() only works after the request (POST) and before calling hhtp.end()
                Serial.printf("HTTP %d\n", httpCode);
                if (httpCode > 0) {
                    Serial.print("Response body: ");
                    Serial.println(http.getString());
                } else {
                    Serial.print("POST failed, error: ");
                    Serial.println(http.errorToString(httpCode));  
                }
    
                http.end();
            }
            //if WiFi disconnected, retry(every 20s to avoid spam)
            else{
                static unsigned long lastWiFiRetry = 0;
                if(millis() - lastWiFiRetry > 20000){
                    lastWiFiRetry = millis();
                    Serial.println("WiFi disconnected - attempting reconnect...");
                    connectWiFi();
                }
            }
        }
    }        
}
