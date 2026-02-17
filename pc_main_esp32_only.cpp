// FILE NAME: pc_main_esp32_only.cpp
// pc_main_esp32_only.cpp server will not generate vitals; it only receives them
#include "WebServer.h"
#include "Risk_Assessment.h"
#include <iostream>

int main() {
    // Start-up defaults (won’t be used once ESP32 sends)
    Vitals initial{};
    initial.HR = -1;
    initial.SpO2 = -1;
    initial.Temp = -1;
    initial.Resp = -1;
    initial.BP_sys = -1;
    initial.BP_dia = -1;

    // CRITICAL: do NOT call setLiveVitalsSource()
    // CRITICAL: no simulation thread

    std::cout << "[server] ESP32-only mode. Waiting for /api/ingest packets...\n";
    startWebServer(initial, "");
    return 0;
}
