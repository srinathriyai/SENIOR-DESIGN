#include "WebServer.h"
#include <iostream>
#include <sstream>
#include <string>

static std::string riskColor(int risk) {
    switch (risk) {
        case 0: return "#4CAF50"; // green
        case 1: return "#FFC107"; // yellow
        case 2: return "#FF5722"; // orange
        default: return "#D32F2F"; // red
    }
}

void startWebServer(const Vitals& current, const std::string& llmResponse) {
    httplib::Server svr;

    svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {

        // Extract colors for each vital
        std::string hrColor   = riskColor(calc_HR_risk(current.HR));
        std::string spo2Color = riskColor(calc_SpO2_risk(current.SpO2));
        std::string tempColor = riskColor(calc_Temp_risk(current.Temp));
        std::string respColor = riskColor(calc_Resp_risk(current.Resp));
        std::string bpColor   = riskColor(calc_BP_risk(current.BP_sys, current.BP_dia));

        // Clean JSON-ish output for display
        std::string cleanResp = llmResponse;
        // Remove newline escapes visually
        for (size_t pos = 0; (pos = cleanResp.find("\\n", pos)) != std::string::npos; ) {
            cleanResp.replace(pos, 2, "<br>");
        }

        std::string html = R"(
        <html>
        <head>
            <title>Vitals Dashboard</title>
            <style>
                body {
                    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
                    margin: 20px;
                    background: #f0f2f5;
                }
                .container {
                    max-width: 900px;
                    margin: auto;
                }
                .card {
                    background: white;
                    padding: 25px;
                    border-radius: 18px;
                    margin-bottom: 25px;
                    box-shadow: 0 6px 18px rgba(0,0,0,0.08);
                }
                h1 {
                    text-align: center;
                    margin-bottom: 25px;
                }
                .section-title {
                    font-size: 20px;
                    margin-bottom: 10px;
                    font-weight: 600;
                    border-left: 5px solid #6a5acd;
                    padding-left: 10px;
                }
                .row {
                    display: flex;
                    gap: 20px;
                    flex-wrap: wrap;
                }
                .vital-box {
                    flex: 1;
                    min-width: 150px;
                    background: #fafafa;
                    padding: 15px;
                    border-radius: 14px;
                    box-shadow: inset 0 0 4px rgba(0,0,0,0.05);
                    text-align: center;
                }
                .risk-dot {
                    display: inline-block;
                    width: 12px;
                    height: 12px;
                    border-radius: 50%;
                    margin-left: 5px;
                }
                .gauge-circle {
                    width: 120px;
                    height: 120px;
                    border-radius: 50%;
                    margin: auto;
                    border: 14px solid #ddd;
                    position: relative;
                }
                .gauge-inner {
                    position: absolute;
                    top: 50%;
                    left: 50%;
                    transform: translate(-50%, -50%);
                    font-size: 22px;
                    font-weight: 700;
                }
                .thermo-bar {
                    height: 20px;
                    border-radius: 10px;
                    background: #ddd;
                    overflow: hidden;
                }
                .thermo-fill {
                    height: 100%;
                    border-radius: 10px;
                }
                .waveform {
                    width: 100%;
                    height: 50px;
                }
                .summary-text {
                    font-size: 15px;
                    line-height: 1.45;
                }
            </style>
        </head>

        <body>
        <div class='container'>
            <h1>Vitals Monitoring Dashboard</h1>

            <!-- HR Gauge -->
            <div class='card'>
                <div class='section-title'>Heart Rate</div>
                <div class='gauge-circle' style='border-color: )" + hrColor + R"(;'>
                    <div class='gauge-inner'>" + std::to_string((int)current.HR) + R"( bpm</div>
                </div>
            </div>

            <!-- Temperature Thermometer Bar -->
            <div class='card'>
                <div class='section-title'>Temperature</div>
                <div>Current: <b>)" + std::to_string(current.Temp) + R"( °C</b></div>
                <div class='thermo-bar'>
                    <div class='thermo-fill' style='width: )" + std::to_string((current.Temp / 42.0) * 100) + R"(%; background: )" + tempColor + R"(;'></div>
                </div>
            </div>

            <!-- Pulse Waveform -->
            <div class='card'>
                <div class='section-title'>Pulse Waveform</div>
                <svg class='waveform'>
                    <polyline points='0,25 20,10 40,40 60,15 80,30 100,10 120,30 140,5 160,25 180,10 200,25' 
                              style='fill:none;stroke:)"
                              + hrColor +
                              R"(;stroke-width:3;' />
                </svg>
            </div>

            <!-- Vitals Boxes -->
            <div class='card'>
                <div class='section-title'>All Vitals</div>
                <div class='row'>
                    <div class='vital-box'>SpO₂<br><b>)" + std::to_string((int)current.SpO2) + R"( %</b>
                    <span class='risk-dot' style='background:)"+ spo2Color +R"(';'></span></div>

                    <div class='vital-box'>Resp<br><b>)" + std::to_string((int)current.Resp) + R"( rpm</b>
                    <span class='risk-dot' style='background:)" + respColor + R"(';'></span></div>

                    <div class='vital-box'>BP<br><b>)" + std::to_string((int)current.BP_sys) + "/" + std::to_string((int)current.BP_dia) + R"(</b>
                    <span class='risk-dot' style='background:)" + bpColor + R"(';'></span></div>
                </div>
            </div>

            <!-- Summary -->
            <div class='card'>
                <div class='section-title'>Clinical Summary</div>
                <div class='summary-text'>" + cleanResp + R"(</div>
            </div>

        </div>
        </body></html>
        )";

        res.set_content(html, "text/html");
    });

    std::cout << "Web UI running at http://localhost:8000\n";
    svr.listen("0.0.0.0", 8000);
}
