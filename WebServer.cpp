#include "WebServer.h"          // Include header for startWebServer() and Vitals struct
#include <iostream>             // For console output
#include <map>                  // For std::map used in risk calculation

// -------------------------------
// Convert risk score (0–3) → color hex code
// Modify these colors if you want different visual themes
// -------------------------------
std::string riskColor(int risk) {
    switch (risk) {
        case 0: return "#2ecc71"; // green = normal
        case 1: return "#f1c40f"; // yellow = mild
        case 2: return "#e67e22"; // orange = moderate
        case 3: return "#e74c3c"; // red = severe
        default: return "#bdc3c7"; // grey = fallback
    }
}

// ====================================================================
// MAIN UI SERVER FUNCTION
// Called from main() to launch the Chrome dashboard
// ====================================================================
void startWebServer(const Vitals& current, const std::string& llmResponse) {

    httplib::Server svr;  // Create a web server instance

    // =============================
    // ROUTE: GET /
    // This builds and sends the HTML UI for the dashboard
    // =============================
    svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {
        
        // -------------------------------------------------------
        // Compute risk for each vital dynamically
        // If you change threshold formulas, the colors update here
        // -------------------------------------------------------
        std::map<std::string, int> risk = {
            {"HR",   calc_HR_risk(current.HR)},
            {"SpO2", calc_SpO2_risk(current.SpO2)},
            {"Temp", calc_Temp_risk(current.Temp)},
            {"Resp", calc_Resp_risk(current.Resp)},
            {"BP",   calc_BP_risk(current.BP_sys, current.BP_dia)}
        };

        // --------------------------------------
        // START OF HTML DOCUMENT STRING
        // Edit CSS inside <style> if you want to change appearance
        // --------------------------------------
        std::string html = R"(
        <html>
        <head>
            <title>Vitals Dashboard</title>

            <!-- ===========================
                 GLOBAL CSS STYLE SECTION
                 You can modify:
                 - background color
                 - font sizes
                 - card spacing
                 - grid layout
               ============================ -->
            <style>

                body {
                    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
                    margin: 0;          
                    background: #f2f3f5;  /* Change this for entire page background */
                    padding: 40px;
                }

                .container {
                    max-width: 800px;     /* Max width of dashboard */
                    margin: auto;
                }

                h2 {
                    text-align: center;
                    margin-bottom: 25px;
                    font-weight: 700;
                }

                /* 2-column layout for vitals */
                .grid {
                    display: grid;
                    grid-template-columns: 1fr 1fr;  /* Two equal columns */
                    gap: 20px;                       /* Spacing between cards */
                }

                /* Main boxes around each vital */
                .card {
                    background: white;               /* White card background */
                    padding: 20px;
                    border-radius: 15px;             /* Rounded corners */
                    box-shadow: 0 4px 12px rgba(0,0,0,0.08); /* Soft shadow */
                }

                /* Label above each vital number */
                .vital-name {
                    font-size: 16px;
                    font-weight: 600;
                }

                /* The big number (like 110 bpm) */
                .vital-value {
                    font-size: 32px;
                    font-weight: 700;
                    margin-top: 5px;
                }

                /* The colored risk tag below the number */
                .badge {
                    display: inline-block;
                    padding: 4px 10px;
                    color: white;
                    border-radius: 8px;
                    font-size: 13px;
                    margin-top: 6px;
                }

                /* The summary card at the bottom */
                .summary-card {
                    margin-top: 30px;
                }

                /* The block of text for the LLM summary */
                .summary-content {
                    white-space: pre-wrap;  /* Preserve newlines */
                    margin-top: 10px;
                    line-height: 1.5;       /* Spacing between lines */
                }
            </style>
        </head>

        <!-- ===========================
             BODY SECTION
             Everything visible on screen
             ============================ -->
        <body>
            <div class="container">
                <h2>Vitals Monitoring Dashboard</h2>

                <!-- 2-column vitals grid -->
                <div class="grid">
        )";

        // -------------------------------
        // Insert dynamic cards for each vital
        // To change layout: modify this HTML block
        // -------------------------------

        // HEART RATE CARD
        html += "<div class='card'><div class='vital-name'>Heart Rate</div>";
        html += "<div class='vital-value'>" + std::to_string((int)current.HR) + " bpm</div>";
        html += "<div class='badge' style='background:" + riskColor(risk["HR"]) + "'>Risk " + std::to_string(risk["HR"]) + "</div></div>";

        // SpO2 CARD
        html += "<div class='card'><div class='vital-name'>SpO₂</div>";
        html += "<div class='vital-value'>" + std::to_string((int)current.SpO2) + " %</div>";
        html += "<div class='badge' style='background:" + riskColor(risk["SpO2"]) + "'>Risk " + std::to_string(risk["SpO2"]) + "</div></div>";

        // TEMPERATURE CARD
        html += "<div class='card'><div class='vital-name'>Temperature</div>";
        html += "<div class='vital-value'>" + std::to_string(current.Temp) + " °C</div>";
        html += "<div class='badge' style='background:" + riskColor(risk["Temp"]) + "'>Risk " + std::to_string(risk["Temp"]) + "</div></div>";

        // RESPIRATORY RATE CARD
        html += "<div class='card'><div class='vital-name'>Respiratory Rate</div>";
        html += "<div class='vital-value'>" + std::to_string((int)current.Resp) + " rpm</div>";
        html += "<div class='badge' style='background:" + riskColor(risk["Resp"]) + "'>Risk " + std::to_string(risk["Resp"]) + "</div></div>";

        // BLOOD PRESSURE CARD
        html += "<div class='card'><div class='vital-name'>Blood Pressure</div>";
        html += "<div class='vital-value'>" + std::to_string((int)current.BP_sys) 
             + "/" + std::to_string((int)current.BP_dia) + "</div>";
        html += "<div class='badge' style='background:" + riskColor(risk["BP"]) + "'>Risk " 
             + std::to_string(risk["BP"]) + "</div></div>";

        // ----------------------------------------------
        // ADD SUMMARY SECTION
        // You can style it just like any other card
        // ----------------------------------------------
        html += R"(
                </div>

                <div class="card summary-card">
                    <div class="vital-name">Clinical Summary</div>
                    <div class="summary-content">
        )";

        // Insert the raw LLM response text
        html += llmResponse;

        // Close HTML structure
        html += R"(
                    </div>
                </div>
            </div>
        </body>
        </html>
        )";

        // Send result back to browser
        res.set_content(html, "text/html");
    });

    // -------------------------------------------
    // Start listening on localhost:8000
    // Change port here if you want a new one
    // -------------------------------------------
    std::cout << "Web UI running at http://localhost:8000\n";
    svr.listen("0.0.0.0", 8000);
}
