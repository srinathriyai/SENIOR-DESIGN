#include "WebServer.h"
#include <iostream>
#include <map>

// Convert risk score to color
std::string riskColor(int risk) {
    switch (risk) {
        case 0: return "#2ecc71"; // green
        case 1: return "#f1c40f"; // yellow
        case 2: return "#e67e22"; // orange
        case 3: return "#e74c3c"; // red
        default: return "#bdc3c7"; // fallback grey
    }
}

void startWebServer(const Vitals& current, const std::string& llmResponse) {
    httplib::Server svr;

    svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {
        
        // Generate risk colors
        std::map<std::string, int> risk = {
            {"HR", calc_HR_risk(current.HR)},
            {"SpO2", calc_SpO2_risk(current.SpO2)},
            {"Temp", calc_Temp_risk(current.Temp)},
            {"Resp", calc_Resp_risk(current.Resp)},
            {"BP", calc_BP_risk(current.BP_sys, current.BP_dia)}
        };

        std::string html = R"(
        <html>
        <head>
            <title>Vitals Dashboard</title>
            <style>
                body {
                    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
                    margin: 0;
                    background: #f2f3f5;
                    padding: 40px;
                }

                .container {
                    max-width: 800px;
                    margin: auto;
                }

                h2 {
                    text-align: center;
                    margin-bottom: 25px;
                    font-weight: 700;
                }

                .grid {
                    display: grid;
                    grid-template-columns: 1fr 1fr;
                    gap: 20px;
                }

                .card {
                    background: white;
                    padding: 20px;
                    border-radius: 15px;
                    box-shadow: 0 4px 12px rgba(0,0,0,0.08);
                }

                .vital-name {
                    font-size: 16px;
                    font-weight: 600;
                }

                .vital-value {
                    font-size: 32px;
                    font-weight: 700;
                    margin-top: 5px;
                }

                .badge {
                    display: inline-block;
                    padding: 4px 10px;
                    color: white;
                    border-radius: 8px;
                    font-size: 13px;
                    margin-top: 6px;
                }

                .summary-card {
                    margin-top: 30px;
                }

                .summary-content {
                    white-space: pre-wrap;
                    margin-top: 10px;
                    line-height: 1.5;
                }
            </style>
        </head>
        <body>
            <div class="container">
                <h2>Vitals Monitoring Dashboard</h2>

                <div class="grid">
        )";

        // ---- Insert dynamic vital cards ----
        html += "<div class='card'><div class='vital-name'>Heart Rate</div>";
        html += "<div class='vital-value'>" + std::to_string((int)current.HR) + " bpm</div>";
        html += "<div class='badge' style='background:" + riskColor(risk["HR"]) + "'>Risk " + std::to_string(risk["HR"]) + "</div></div>";

        html += "<div class='card'><div class='vital-name'>SpO₂</div>";
        html += "<div class='vital-value'>" + std::to_string((int)current.SpO2) + " %</div>";
        html += "<div class='badge' style='background:" + riskColor(risk["SpO2"]) + "'>Risk " + std::to_string(risk["SpO2"]) + "</div></div>";

        html += "<div class='card'><div class='vital-name'>Temperature</div>";
        html += "<div class='vital-value'>" + std::to_string(current.Temp) + " °C</div>";
        html += "<div class='badge' style='background:" + riskColor(risk["Temp"]) + "'>Risk " + std::to_string(risk["Temp"]) + "</div></div>";

        html += "<div class='card'><div class='vital-name'>Respiratory Rate</div>";
        html += "<div class='vital-value'>" + std::to_string((int)current.Resp) + " rpm</div>";
        html += "<div class='badge' style='background:" + riskColor(risk["Resp"]) + "'>Risk " + std::to_string(risk["Resp"]) + "</div></div>";

        html += "<div class='card'><div class='vital-name'>Blood Pressure</div>";
        html += "<div class='vital-value'>" + std::to_string((int)current.BP_sys) + "/" + std::to_string((int)current.BP_dia) + "</div>";
        html += "<div class='badge' style='background:" + riskColor(risk["BP"]) + "'>Risk " + std::to_string(risk["BP"]) + "</div></div>";

        // ---- Summary Card ----
        html += R"(
                </div>

                <div class="card summary-card">
                    <div class="vital-name">Clinical Summary</div>
                    <div class="summary-content">
        )";

        html += llmResponse;

        html += R"(
                    </div>
                </div>
            </div>
        </body>
        </html>
        )";

        res.set_content(html, "text/html");
    });

    std::cout << "Web UI running at http://localhost:8000\n";
    svr.listen("0.0.0.0", 8000);
}
