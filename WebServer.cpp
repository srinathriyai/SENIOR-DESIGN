#include "WebServer.h"          // startWebServer() + Vitals struct + risk funcs
#include "LLM.h"                // sendToLLM(), generatePrompt(), extractContent()
#include <iostream>
#include <map>

// ---- Live vitals source (set by pc_main.cpp) ----
static const Vitals* g_liveVitals = nullptr;

void setLiveVitalsSource(const Vitals* live) {
    g_liveVitals = live;
}

// -------------------------------
// Convert risk score (0–3) → color hex code
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

static std::string htmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c; break;
        }
    }
    return out;
}

// ====================================================================
// MAIN UI SERVER FUNCTION
// Called from main() to launch the dashboard at http://localhost:8000
// ====================================================================
void startWebServer(const Vitals& current, const std::string& llmResponse) {

    httplib::Server svr;

    // =========================================================
    // ROUTE: GET /api/vitals
    // Returns latest vitals + risk as JSON (for live UI updates)
    // BRIDGE BTW BACKEND AND FRONT END
    // =========================================================
    svr.Get("/api/vitals", [&](const httplib::Request&, httplib::Response& res) {

        // reads whatever values are currently in live.
        const Vitals& v = (g_liveVitals ? *g_liveVitals : current);

        std::map<std::string, int> risk = {
            {"HR",   calc_HR_risk(v.HR)},
            {"SpO2", calc_SpO2_risk(v.SpO2)},
            {"Temp", calc_Temp_risk(v.Temp)},
            {"Resp", calc_Resp_risk(v.Resp)},
            {"BP",   calc_BP_risk(v.BP_sys, v.BP_dia)}
        };

        std::string json = "{";
        json += "\"hr\":" + std::to_string(v.HR) + ",";
        json += "\"spo2\":" + std::to_string(v.SpO2) + ",";
        json += "\"temp\":" + std::to_string(v.Temp) + ",";
        json += "\"resp\":" + std::to_string(v.Resp) + ",";
        json += "\"sys\":" + std::to_string(v.BP_sys) + ",";
        json += "\"dia\":" + std::to_string(v.BP_dia) + ",";

        json += "\"risk_hr\":" + std::to_string(risk["HR"]) + ",";
        json += "\"risk_spo2\":" + std::to_string(risk["SpO2"]) + ",";
        json += "\"risk_temp\":" + std::to_string(risk["Temp"]) + ",";
        json += "\"risk_resp\":" + std::to_string(risk["Resp"]) + ",";
        json += "\"risk_bp\":" + std::to_string(risk["BP"]);
        json += "}";

        res.set_content(json, "application/json; charset=utf-8");
    });

    // =========================================================
    // ROUTE: GET /api/summary
    // Generates a NEW clinical summary by calling llama-server.
    // Returns plain text.
    // =========================================================
    svr.Get("/api/summary", [&](const httplib::Request&, httplib::Response& res) {

        const Vitals& v = (g_liveVitals ? *g_liveVitals : current);

        std::map<std::string, int> risk = {
            {"HR",   calc_HR_risk(v.HR)},
            {"SpO2", calc_SpO2_risk(v.SpO2)},
            {"Temp", calc_Temp_risk(v.Temp)},
            {"Resp", calc_Resp_risk(v.Resp)},
            {"BP",   calc_BP_risk(v.BP_sys, v.BP_dia)}
        };

        std::string prompt  = generatePrompt(v, risk);
        std::string rawJson = sendToLLM(prompt);
        std::string summary = extractContent(rawJson);

        res.set_content(summary, "text/plain; charset=utf-8");
    });

    // =============================
    // ROUTE: GET /
    // Serves the HTML UI dashboard
    // =============================
    svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {

        std::map<std::string, int> risk = {
            {"HR",   calc_HR_risk(current.HR)},
            {"SpO2", calc_SpO2_risk(current.SpO2)},
            {"Temp", calc_Temp_risk(current.Temp)},
            {"Resp", calc_Resp_risk(current.Resp)},
            {"BP",   calc_BP_risk(current.BP_sys, current.BP_dia)}
        };

        std::string html = R"HTML(
        <html>
        <head>
            <meta charset="UTF-8">
            <title>Vitals Dashboard</title>
            <style>
                body {
                    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
                    margin: 0;
                    background: #f2f3f5;
                }

                .app {
                    display: flex;
                    min-height: 100vh;
                }

                .sidebar {
                    width: 250px;
                    background: #ffffff;
                    border-right: 1px solid rgba(0,0,0,0.08);
                    padding: 18px 14px;
                    box-sizing: border-box;
                }

                .brand {
                    font-weight: 800;
                    font-size: 16px;
                    margin-bottom: 14px;
                }

                .nav {
                    display: flex;
                    flex-direction: column;
                    gap: 10px;
                    margin-top: 10px;
                }

                .nav button {
                    text-align: left;
                    width: 100%;
                    padding: 10px 12px;
                    border: 1px solid rgba(0,0,0,0.08);
                    border-radius: 12px;
                    background: #fff;
                    cursor: pointer;
                    font-size: 14px;
                }

                .nav button.active {
                    background: #f5f7ff;
                    border-color: rgba(0,0,0,0.12);
                    font-weight: 700;
                }

                .content {
                    flex: 1;
                    padding: 30px 40px;
                    box-sizing: border-box;
                }

                .topbar {
                    display: flex;
                    align-items: center;
                    gap: 10px;
                    margin-bottom: 18px;
                }

                .back-btn {
                    border: 1px solid rgba(0,0,0,0.12);
                    background: #fff;
                    border-radius: 12px;
                    padding: 8px 12px;
                    cursor: pointer;
                    font-size: 14px;
                }

                .page-title {
                    font-weight: 800;
                    font-size: 20px;
                    margin: 0;
                }

                .muted {
                    opacity: 0.7;
                    font-size: 13px;
                }

                .page { display: none; }
                .page.active { display: block; }

                .list {
                    display: flex;
                    flex-direction: column;
                    gap: 10px;
                    margin-top: 14px;
                }

                .list-item {
                    background: #fff;
                    border: 1px solid rgba(0,0,0,0.08);
                    border-radius: 14px;
                    padding: 12px 14px;
                    cursor: pointer;
                }

                .container {
                    max-width: 900px;
                    margin: 0;
                }

                h2 {
                    text-align: left;
                    margin: 0 0 8px 0;
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

                .summary-card { margin-top: 30px; }
                .summary-content { white-space: pre-wrap; margin-top: 10px; line-height: 1.5; }
            </style>
        </head>

        <body>
            <div class="app">

                <aside class="sidebar">
                    <div class="brand">Vitals Monitor</div>

                    <div class="nav">
                        <button id="navPatients" class="active" onclick="go('patients')">Patients</button>
                        <button id="navLive" onclick="go('live')">Live Vitals</button>
                        <button id="navReports" onclick="go('reports')">Reports</button>
                    </div>

                    <div style="margin-top:16px;" class="muted">
                        Select a patient, then view live vitals.
                    </div>
                </aside>

                <main class="content">
                    <div class="topbar">
                        <button class="back-btn" onclick="back()">← Back</button>
                        <h1 id="pageTitle" class="page-title">Patients</h1>
                    </div>

                    <section id="pagePatients" class="page active">
                        <div class="card">
                            <div class="vital-name">Select a patient</div>
                            <div class="list">
                                <div class="list-item" onclick="selectPatient('Patient A (001)')">
                                    <div><b>Patient A</b></div>
                                    <div class="muted">ID: 001 • Last update: just now</div>
                                </div>
                                <div class="list-item" onclick="selectPatient('Patient B (002)')">
                                    <div><b>Patient B</b></div>
                                    <div class="muted">ID: 002 • Last update: 5 min ago</div>
                                </div>
                            </div>
                        </div>
                    </section>

                    <section id="pageLive" class="page">
                        <div class="container">
                            <h2>Live Vitals</h2>
                            <div class="muted" style="margin-bottom:12px;">
                                Selected patient: <span id="selectedPatientLabel">None</span>
                            </div>

                            <div class="grid">
        )HTML";

        // HEART RATE CARD
        html += "<div class='card'><div class='vital-name'>Heart Rate</div>";
        html += "<div id='hrValue' class='vital-value'>" + std::to_string((int)current.HR) + " bpm</div>";
        html += "<div id='hrRisk' class='badge' style='background:" + riskColor(risk["HR"]) + "'>Risk " + std::to_string(risk["HR"]) + "</div></div>";

        // SpO2 CARD
        html += "<div class='card'><div class='vital-name'>SpO₂</div>";
        html += "<div id='spo2Value' class='vital-value'>" + std::to_string((int)current.SpO2) + " %</div>";
        html += "<div id='spo2Risk' class='badge' style='background:" + riskColor(risk["SpO2"]) + "'>Risk " + std::to_string(risk["SpO2"]) + "</div></div>";

        // TEMPERATURE CARD
        html += "<div class='card'><div class='vital-name'>Temperature</div>";
        html += "<div id='tempValue' class='vital-value'>" + std::to_string(current.Temp) + " °C</div>";
        html += "<div id='tempRisk' class='badge' style='background:" + riskColor(risk["Temp"]) + "'>Risk " + std::to_string(risk["Temp"]) + "</div></div>";

        // RESPIRATORY RATE CARD
        html += "<div class='card'><div class='vital-name'>Respiratory Rate</div>";
        html += "<div id='respValue' class='vital-value'>" + std::to_string((int)current.Resp) + " rpm</div>";
        html += "<div id='respRisk' class='badge' style='background:" + riskColor(risk["Resp"]) + "'>Risk " + std::to_string(risk["Resp"]) + "</div></div>";

        // BLOOD PRESSURE CARD
        html += "<div class='card'><div class='vital-name'>Blood Pressure</div>";
        html += "<div id='bpValue' class='vital-value'>" + std::to_string((int)current.BP_sys)
             + "/" + std::to_string((int)current.BP_dia) + " mmHg</div>";
        html += "<div id='bpRisk' class='badge' style='background:" + riskColor(risk["BP"]) + "'>Risk "
             + std::to_string(risk["BP"]) + "</div></div>";

        // Summary section + JS
        html += R"HTML(
                            </div>

                            <div class="card summary-card">
                                <div class="vital-name">Clinical Summary</div>
                                <div id="summaryText" class="summary-content">
        )HTML";

        // Initial summary shown immediately
        html += htmlEscape(llmResponse);

        html += R"HTML(
                                </div>
                                <div id="summaryStatus" class="muted" style="margin-top:10px;"></div>
                            </div>
                        </div>
                    </section>

                    <section id="pageReports" class="page">
                        <div class="card">
                            <div class="vital-name">Reports</div>
                            <div class="muted">Later: list saved summaries / exports / risk logs</div>

                            <div class="list">
                                <div class="list-item">
                                    <b>Report 2026-01-22</b>
                                    <div class="muted">Summary + risk snapshot</div>
                                </div>
                                <div class="list-item">
                                    <b>Report 2026-01-20</b>
                                    <div class="muted">Vitals trend + notes</div>
                                </div>
                            </div>
                        </div>
                    </section>

                    <script>
                        const historyStack = [];
                        let currentPage = 'patients';
                        let selectedPatient = null;

                        function setActiveNav(page) {
                            document.getElementById('navPatients').classList.toggle('active', page === 'patients');
                            document.getElementById('navLive').classList.toggle('active', page === 'live');
                            document.getElementById('navReports').classList.toggle('active', page === 'reports');
                        }

                        function setPageTitle(page) {
                            const title = (page === 'patients') ? 'Patients'
                                         : (page === 'live') ? 'Live Vitals'
                                         : 'Reports';
                            document.getElementById('pageTitle').textContent = title;
                        }

                        function showPage(page) {
                            document.getElementById('pagePatients').classList.toggle('active', page === 'patients');
                            document.getElementById('pageLive').classList.toggle('active', page === 'live');
                            document.getElementById('pageReports').classList.toggle('active', page === 'reports');
                            currentPage = page;
                            setActiveNav(page);
                            setPageTitle(page);
                        }

                        function go(page) {
                            if (page === currentPage) return;
                            historyStack.push(currentPage);
                            showPage(page);
                        }

                        function back() {
                            if (historyStack.length === 0) return;
                            const prev = historyStack.pop();
                            showPage(prev);
                        }

                        function selectPatient(name) {
                            selectedPatient = name;
                            document.getElementById('selectedPatientLabel').textContent = name;
                            historyStack.push(currentPage);
                            showPage('live');
                        }

                        function riskColorJS(r) {
                            if (r === 0) return "#2ecc71";
                            if (r === 1) return "#f1c40f";
                            if (r === 2) return "#e67e22";
                            return "#e74c3c";
                        }

                        async function refreshVitals() {
                            try {
                                const r = await fetch('/api/vitals');
                                const v = await r.json();

                                document.getElementById('hrValue').textContent   = Math.round(v.hr) + " bpm";
                                document.getElementById('spo2Value').textContent = Math.round(v.spo2) + " %";
                                document.getElementById('tempValue').textContent = Number(v.temp).toFixed(1) + " °C";
                                document.getElementById('respValue').textContent = Math.round(v.resp) + " rpm";
                                document.getElementById('bpValue').textContent   = Math.round(v.sys) + "/" + Math.round(v.dia) + " mmHg";

                                const hrR = document.getElementById('hrRisk');
                                const spR = document.getElementById('spo2Risk');
                                const tR  = document.getElementById('tempRisk');
                                const rrR = document.getElementById('respRisk');
                                const bpR = document.getElementById('bpRisk');

                                hrR.textContent = "Risk " + v.risk_hr;    hrR.style.background = riskColorJS(v.risk_hr);
                                spR.textContent = "Risk " + v.risk_spo2;  spR.style.background = riskColorJS(v.risk_spo2);
                                tR.textContent  = "Risk " + v.risk_temp;  tR.style.background  = riskColorJS(v.risk_temp);
                                rrR.textContent = "Risk " + v.risk_resp;  rrR.style.background = riskColorJS(v.risk_resp);
                                bpR.textContent = "Risk " + v.risk_bp;    bpR.style.background = riskColorJS(v.risk_bp);
                            } catch (e) {
                                // ignore for now
                            }
                        }

                        async function refreshSummary() {
                            const el = document.getElementById('summaryText');
                            const status = document.getElementById('summaryStatus');
                            if (!el) return;

                            if (status) status.textContent = "Updating summary...";

                            try {
                                const r = await fetch('/api/summary');
                                const t = await r.text();
                                el.textContent = t;
                                if (status) status.textContent = "";
                            } catch (e) {
                                if (status) status.textContent = "Update failed.";
                            }
                        }

                        window.addEventListener('load', () => {
                            // start live vitals polling
                            refreshVitals();
                            setInterval(refreshVitals, 1000);

                            // generate summary on load (background)
                            setTimeout(refreshSummary, 100);
                        });
                    </script>

                </main>
            </div>
        </body>
        </html>
        )HTML";

        res.set_content(html, "text/html; charset=utf-8");
    });

    std::cout << "Web UI running at http://localhost:8000\n";
    svr.listen("0.0.0.0", 8000);
}
