#include "WebServer.h"
#include "LLM.h"
#include <map>
#include <string>
#include <thread>
#include <chrono>
#include <cmath>
#include <iostream>

// small helper to clamp values
static float clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

int main() {
    // Live vitals struct (this is what will update over time)
    //DATA ALWAYS CHANGES IN MEMORY
    Vitals live{};
    live.HR = 75;
    live.SpO2 = 94;
    live.Temp = 38.5f;
    live.Resp = 34;
    live.BP_sys = 150;
    live.BP_dia = 95;

    // Give WebServer.cpp access to live-updating vitals
    // when this gets called, whenever vitals are needed, we read this struct.
    setLiveVitalsSource(&live); // refer to webserver.cpp

    // Compute initial risk map (for initial prompt)
    std::map<std::string, int> risk = {
        {"HR",   calc_HR_risk(live.HR)},
        {"SpO2", calc_SpO2_risk(live.SpO2)},
        {"Temp", calc_Temp_risk(live.Temp)},
        {"Resp", calc_Resp_risk(live.Resp)},
        {"BP",   calc_BP_risk(live.BP_sys, live.BP_dia)}
    };

    // Initial LLM text shown on page load
    // (still slow; later we can cache or generate in background)
    std::string initial = extractContent(sendToLLM(generatePrompt(live, risk)));

    // Optional debug prints (one-time)
    std::cout << "Initial risks:\n";
    std::cout << "  HR:   " << calc_HR_risk(live.HR) << "\n";
    std::cout << "  SpO2: " << calc_SpO2_risk(live.SpO2) << "\n";
    std::cout << "  Temp: " << calc_Temp_risk(live.Temp) << "\n";
    std::cout << "  Resp: " << calc_Resp_risk(live.Resp) << "\n";
    std::cout << "  BP:   " << calc_BP_risk(live.BP_sys, live.BP_dia) << "\n";

    // Background simulator: updates vitals every 1 second
    std::thread sim([&]() {
        float t = 0.0f;

        while (true) {
            t += 0.12f;

            // These are designed to cross risk thresholds occasionally
            live.HR   = clampf(85.0f + 50.0f * std::sin(t), 35.0f, 150.0f);
            live.SpO2 = clampf(96.0f - 12.0f * (0.5f + 0.5f * std::sin(t * 0.7f)), 82.0f, 99.0f);
            live.Temp = clampf(37.2f + 2.2f * std::sin(t * 0.5f), 34.5f, 40.5f);
            live.Resp = clampf(16.0f + 20.0f * (0.5f + 0.5f * std::sin(t * 0.9f)), 6.0f, 40.0f);

            // BP oscillates (keeps sys/dia in reasonable relation)
            live.BP_sys = clampf(115.0f + 70.0f * (0.5f + 0.5f * std::sin(t * 0.6f)), 80.0f, 185.0f);
            live.BP_dia = clampf(75.0f  + 45.0f * (0.5f + 0.5f * std::sin(t * 0.6f)), 45.0f, 120.0f);

            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    });

    // Start dashboard on localhost:8000 (this blocks)
    startWebServer(live, initial);

    // (Won't reach here until server stops)
    sim.join();
    return 0;
}
