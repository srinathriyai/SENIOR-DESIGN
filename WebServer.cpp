#include "WebServer.h"
#include <iostream>

void startWebServer(const Vitals& current, const std::string& llmResponse) {
    httplib::Server svr;

    svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {
        std::string html = R"(
        <html>
        <head>
            <title>Vitals Dashboard</title>
            <style>
                body {
                    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
                    margin: 40px;
                    background: #f5f5f7;
                }
                .card {
                    background: white;
                    padding: 20px;
                    border-radius: 20px;
                    box-shadow: 0 8px 20px rgba(0,0,0,0.1);
                    max-width: 600px;
                    margin: auto;
                }
                h2 {
                    text-align: center;
                    font-weight: 700;
                }
                .vital {
                    font-size: 18px;
                    margin-bottom: 12px;
                }
                .summary {
                    margin-top: 20px;
                    font-size: 16px;
                    white-space: pre-wrap;
                }
            </style>
        </head>
        <body>
            <div class='card'>
                <h2>Vitals Monitoring Dashboard</h2>
        )";

        // Insert vitals dynamically
        html += "<div class='vital'>HR: " + std::to_string(static_cast<int>(current.HR)) + " bpm</div>";
        html += "<div class='vital'>SpO₂: " + std::to_string(static_cast<int>(current.SpO2)) + " %</div>";
        html += "<div class='vital'>Temp: " + std::to_string(current.Temp) + " °C</div>";
        html += "<div class='vital'>Resp: " + std::to_string(static_cast<int>(current.Resp)) + " rpm</div>";
        html += "<div class='vital'>BP: " + std::to_string(static_cast<int>(current.BP_sys)) +
                "/" + std::to_string(static_cast<int>(current.BP_dia)) + " mmHg</div>";

        // Add summary
        html += "<h3>Clinical Summary</h3>";
        html += "<div class='summary'>" + llmResponse + "</div>";

        // Close card + body
        html += R"(
            </div>
        </body>
        </html>
        )";

        res.set_content(html, "text/html");
    });

    std::cout << "Web UI running at http://localhost:5000\n";
    svr.listen("0.0.0.0", 8000);
}
