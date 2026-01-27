#include "WebServer.h"          // startWebServer() + Vitals struct + risk funcs
#include "LLM.h"                // sendToLLM(), generatePrompt(), extractContent()
#include <iostream>
#include <map>

<<<<<<< Updated upstream
// ---- Live vitals source (set by pc_main.cpp) ----
=======
// ============================================================================
// WebServer.cpp ((((UPDATED 1/27/26 3:30PM))))
//
// What this file does:
// 1) Hosts a local web dashboard at: http://localhost:8000
// 2) Exposes API endpoints the dashboard uses:
//    - GET /api/vitals  -> latest vitals + risk levels (JSON)
//    - GET /api/summary -> generates a NEW LLM clinical summary (plain text)
// 3) Serves a single-page dashboard UI from C++ (HTML + JS embedded as a string)
//
// Features in this version:
// - Two patient profiles (A/B): name, gender, age (stored in browser localStorage)
// - One active patient at a time (prevents mixing)
// - "Start Vitals (3:00)" session button:
//    - logs 1 sample/sec for 180 seconds
//    - builds a CSV at end of session
//    - SAVES the CSV under the Reports tab (in localStorage)
//    - user can download later from Reports
// - Locks patient editing/switching while session runs
//
// NOTE: Report saving is done in the browser (localStorage).
//       This is the fastest MVP. If you later want server-side file saving,
//       we can add /api/reports endpoints and write to disk.
//
// ============================================================================

// ---- Live vitals source (set by pc_main.cpp) ----
// If pc_main.cpp calls setLiveVitalsSource(&liveVitals),
// the server will always serve the latest values from that struct.
>>>>>>> Stashed changes
static const Vitals* g_liveVitals = nullptr;

void setLiveVitalsSource(const Vitals* live) {
    g_liveVitals = live;
}

// -------------------------------
// Convert risk score (0–3) → color hex code
<<<<<<< Updated upstream
=======
// Used for UI badges.
>>>>>>> Stashed changes
// -------------------------------
std::string riskColor(int risk) {
    switch (risk) {
        case 0: return "#2ecc71"; // green = normal
        case 1: return "#f1c40f"; // yellow = mild
        case 2: return "#e67e22"; // orange = moderate
        case 3: return "#e74c3c"; // red = severe
        default: return "#bdc3c7"; // grey (fallback)
    }
}

<<<<<<< Updated upstream
=======
// -------------------------------
// Escape text so it can be safely injected into HTML (prevents broken markup)
// Used for initial LLM response that is rendered in the page.
// -------------------------------
>>>>>>> Stashed changes
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
<<<<<<< Updated upstream
    // BRIDGE BTW BACKEND AND FRONT END
    // =========================================================
    svr.Get("/api/vitals", [&](const httplib::Request&, httplib::Response& res) {

        // reads whatever values are currently in live.
        const Vitals& v = (g_liveVitals ? *g_liveVitals : current);

=======
    //
    // Frontend calls this once per second (1Hz) to:
    // - update the displayed vitals
    // - (during a session) log a snapshot for the session CSV
    // =========================================================
    svr.Get("/api/vitals", [&](const httplib::Request&, httplib::Response& res) {

        // Use live pointer if available, otherwise use initial "current"
        const Vitals& v = (g_liveVitals ? *g_liveVitals : current);

        // Risk scoring functions come from Risk_Assessment.h/.cpp
>>>>>>> Stashed changes
        std::map<std::string, int> risk = {
            {"HR",   calc_HR_risk(v.HR)},
            {"SpO2", calc_SpO2_risk(v.SpO2)},
            {"Temp", calc_Temp_risk(v.Temp)},
            {"Resp", calc_Resp_risk(v.Resp)},
            {"BP",   calc_BP_risk(v.BP_sys, v.BP_dia)}
        };

<<<<<<< Updated upstream
=======
        // Build a simple JSON object manually
        // (No JSON library needed for this size.)
>>>>>>> Stashed changes
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
<<<<<<< Updated upstream
=======
    //
    // Frontend calls this once on load (or you can add a refresh button later).
>>>>>>> Stashed changes
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

<<<<<<< Updated upstream
=======
        // Initial risk values used for first render.
        // After page loads, the frontend will poll /api/vitals and update live.
>>>>>>> Stashed changes
        std::map<std::string, int> risk = {
            {"HR",   calc_HR_risk(current.HR)},
            {"SpO2", calc_SpO2_risk(current.SpO2)},
            {"Temp", calc_Temp_risk(current.Temp)},
            {"Resp", calc_Resp_risk(current.Resp)},
            {"BP",   calc_BP_risk(current.BP_sys, current.BP_dia)}
        };

<<<<<<< Updated upstream
=======
        // =====================================================================
        // Build HTML as a single string.
        // NOTE: We mix:
        // - a big raw-string (R"HTML(... )HTML") for most HTML/CSS/JS
        // - plus small string concatenations to inject initial values
        // =====================================================================
>>>>>>> Stashed changes
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
<<<<<<< Updated upstream
                    cursor: pointer;
=======
>>>>>>> Stashed changes
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
<<<<<<< Updated upstream
=======

                /* Small button style for patient cards + session controls */
                .mini-btn {
                    padding: 8px 10px;
                    border-radius: 10px;
                    border: 1px solid rgba(0,0,0,0.12);
                    background: #fff;
                    cursor: pointer;
                    font-size: 13px;
                }
                .mini-btn:disabled {
                    opacity: 0.5;
                    cursor: not-allowed;
                }

                /* Simple inputs */
                .field {
                    width: 100%;
                    padding: 10px;
                    border-radius: 10px;
                    border: 1px solid rgba(0,0,0,0.12);
                    box-sizing: border-box;
                }
>>>>>>> Stashed changes
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
<<<<<<< Updated upstream
                        Select a patient, then view live vitals.
=======
                        Set patient info, select active patient, then start a 3-minute session.
>>>>>>> Stashed changes
                    </div>
                </aside>

                <main class="content">
                    <div class="topbar">
                        <button class="back-btn" onclick="back()">← Back</button>
                        <h1 id="pageTitle" class="page-title">Patients</h1>
                    </div>

<<<<<<< Updated upstream
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
=======
                    <!-- ==========================================================
                         PAGE: PATIENTS
                         - Two profile slots (A + B)
                         - Each profile stores: name, gender, age
                         - Stored in localStorage so refresh keeps data
                         ========================================================== -->
                    <section id="pagePatients" class="page active">
                        <div class="card">
                            <div class="vital-name">Patient Profiles (max 2)</div>
                            <div class="muted" style="margin-top:6px;">
                                Edit a profile, then select it before starting a vitals session.
                            </div>

                            <div class="list">
                                <!-- Patient A -->
                                <div class="list-item">
                                    <div style="display:flex; justify-content:space-between; align-items:center; gap:12px;">
                                        <div>
                                            <div><b>Patient A</b> <span class="muted" id="pA_status"></span></div>
                                            <div class="muted" id="pA_label">Not set</div>
                                        </div>
                                        <div style="display:flex; gap:8px;">
                                            <button class="mini-btn" id="btnEditA" onclick="editPatient(0)">Edit</button>
                                            <button class="mini-btn" id="btnSelectA" onclick="activatePatient(0)">Select</button>
                                        </div>
                                    </div>
                                </div>

                                <!-- Patient B -->
                                <div class="list-item">
                                    <div style="display:flex; justify-content:space-between; align-items:center; gap:12px;">
                                        <div>
                                            <div><b>Patient B</b> <span class="muted" id="pB_status"></span></div>
                                            <div class="muted" id="pB_label">Not set</div>
                                        </div>
                                        <div style="display:flex; gap:8px;">
                                            <button class="mini-btn" id="btnEditB" onclick="editPatient(1)">Edit</button>
                                            <button class="mini-btn" id="btnSelectB" onclick="activatePatient(1)">Select</button>
                                        </div>
                                    </div>
                                </div>
                            </div>

                            <!-- Edit form (hidden until user clicks Edit) -->
                            <div id="patientForm" class="card" style="margin-top:14px; display:none;">
                                <div class="vital-name">Edit patient</div>

                                <div style="display:grid; grid-template-columns:1fr 1fr; gap:10px; margin-top:10px;">
                                    <div>
                                        <div class="muted">Name</div>
                                        <input id="f_name" class="field" placeholder="Jane Doe" />
                                    </div>

                                    <div>
                                        <div class="muted">Age</div>
                                        <input id="f_age" class="field" type="number" min="0" max="120" placeholder="21" />
                                    </div>

                                    <div>
                                        <div class="muted">Gender</div>
                                        <select id="f_gender" class="field">
                                            <option value="">Select…</option>
                                            <option>Female</option>
                                            <option>Male</option>
                                            <option>Non-binary</option>
                                            <option>Prefer not to say</option>
                                        </select>
                                    </div>
                                </div>

                                <div style="display:flex; gap:10px; margin-top:12px;">
                                    <button class="mini-btn" onclick="savePatient()">Save</button>
                                    <button class="mini-btn" onclick="cancelEdit()">Cancel</button>
>>>>>>> Stashed changes
                                </div>
                            </div>
                        </div>
                    </section>

<<<<<<< Updated upstream
=======
                    <!-- ==========================================================
                         PAGE: LIVE
                         - Shows which patient is active
                         - Provides Start Vitals session button:
                           logs 1 sample/sec for 3 minutes
                           builds CSV at end
                           SAVES to Reports tab (not auto-download)
                         - Displays live vitals + risks
                         ========================================================== -->
>>>>>>> Stashed changes
                    <section id="pageLive" class="page">
                        <div class="container">
                            <h2>Live Vitals</h2>
                            <div class="muted" style="margin-bottom:12px;">
<<<<<<< Updated upstream
                                Selected patient: <span id="selectedPatientLabel">None</span>
=======
                                Active patient: <span id="selectedPatientLabel">None</span>
                            </div>

                            <!-- Session controls -->
                            <div class="card" style="margin-bottom:14px;">
                                <div class="vital-name">Vitals Session</div>
                                <div class="muted" style="margin-top:6px;">
                                    Logs 1 sample/sec for 3 minutes, then saves a CSV under Reports.
                                </div>

                                <div style="display:flex; align-items:center; gap:12px; margin-top:12px;">
                                    <button id="startBtn" class="mini-btn" onclick="startVitalsSession()">
                                        Start Vitals (3:00)
                                    </button>
                                    <div class="muted">Time left: <span id="timerLabel">3:00</span></div>
                                    <div class="muted" id="sessionStatus"></div>
                                </div>
>>>>>>> Stashed changes
                            </div>

                            <div class="grid">
        )HTML";
<<<<<<< Updated upstream
=======

        // ==========================================================
        // Initial cards rendered using "current" vitals.
        // Frontend will overwrite these values after first /api/vitals fetch.
        // ==========================================================
>>>>>>> Stashed changes

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

<<<<<<< Updated upstream
        // Summary section + JS
=======
        // ==========================================================
        // Summary section + JS
        // ==========================================================
>>>>>>> Stashed changes
        html += R"HTML(
                            </div>

                            <div class="card summary-card">
                                <div class="vital-name">Clinical Summary</div>
                                <div id="summaryText" class="summary-content">
        )HTML";

<<<<<<< Updated upstream
        // Initial summary shown immediately
=======
        // Initial summary shown immediately (passed into startWebServer)
>>>>>>> Stashed changes
        html += htmlEscape(llmResponse);

        html += R"HTML(
                                </div>
                                <div id="summaryStatus" class="muted" style="margin-top:10px;"></div>
                            </div>
                        </div>
                    </section>

<<<<<<< Updated upstream
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
=======
                    <!-- ==========================================================
                         PAGE: REPORTS
                         - Lists saved CSV exports (stored in localStorage)
                         - Lets user download later
                         ========================================================== -->
                    <section id="pageReports" class="page">
                        <div class="card">
                            <div class="vital-name">Reports</div>
                            <div class="muted">Saved session exports. Download anytime.</div>

                            <div class="list" id="reportsList" style="margin-top:14px;"></div>

                            <div style="margin-top:12px;">
                                <button class="mini-btn" onclick="clearAllReports()">Clear all</button>
>>>>>>> Stashed changes
                            </div>
                        </div>
                    </section>

                    <script>
<<<<<<< Updated upstream
                        const historyStack = [];
                        let currentPage = 'patients';
                        let selectedPatient = null;
=======
                        // ==========================================================
                        // Simple navigation within this single HTML page
                        // ==========================================================
                        const historyStack = [];
                        let currentPage = 'patients';
>>>>>>> Stashed changes

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
<<<<<<< Updated upstream
=======

                            // If user navigates to Reports, refresh the list (safe to call anytime)
                            if (page === 'reports') {
                                renderReports();
                            }
>>>>>>> Stashed changes
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

<<<<<<< Updated upstream
                        function selectPatient(name) {
                            selectedPatient = name;
                            document.getElementById('selectedPatientLabel').textContent = name;
=======
                        // ==========================================================
                        // Patient Profiles (max 2)
                        //
                        // Stored in localStorage so refresh keeps patient info.
                        // activePatientId determines which patient session applies to.
                        // ==========================================================
                        const LS_KEY = "vitals_patients_v1";

                        // Default structure
                        let patients = [
                            { name: "", gender: "", age: "", lastExport: "" }, // Patient A
                            { name: "", gender: "", age: "", lastExport: "" }  // Patient B
                        ];

                        // Which patient is active (0 or 1)
                        let activePatientId = null;

                        // Which patient is being edited (0 or 1)
                        let editingPatientId = null;

                        function loadPatients() {
                            try {
                                const raw = localStorage.getItem(LS_KEY);
                                if (!raw) return;
                                const obj = JSON.parse(raw);
                                if (Array.isArray(obj) && obj.length === 2) patients = obj;
                            } catch (e) {
                                // ignore
                            }
                        }

                        function savePatientsToStorage() {
                            try {
                                localStorage.setItem(LS_KEY, JSON.stringify(patients));
                            } catch (e) {
                                // ignore
                            }
                        }

                        function fmtPatient(p) {
                            if (!p.name || !p.gender || !p.age) return "Not set";
                            return `${p.name} • ${p.gender} • Age ${p.age}`;
                        }

                        function renderPatientsUI() {
                            document.getElementById("pA_label").textContent = fmtPatient(patients[0]);
                            document.getElementById("pB_label").textContent = fmtPatient(patients[1]);

                            document.getElementById("pA_status").textContent =
                                patients[0].lastExport ? `• Last export: ${patients[0].lastExport}` : "";

                            document.getElementById("pB_status").textContent =
                                patients[1].lastExport ? `• Last export: ${patients[1].lastExport}` : "";
                        }

                        function editPatient(id) {
                            if (sessionRunning) return alert("Stop the session before editing.");
                            editingPatientId = id;

                            document.getElementById("patientForm").style.display = "block";
                            document.getElementById("f_name").value = patients[id].name || "";
                            document.getElementById("f_gender").value = patients[id].gender || "";
                            document.getElementById("f_age").value = patients[id].age || "";
                        }

                        function cancelEdit() {
                            editingPatientId = null;
                            document.getElementById("patientForm").style.display = "none";
                        }

                        function savePatient() {
                            const name = document.getElementById("f_name").value.trim();
                            const gender = document.getElementById("f_gender").value.trim();
                            const age = document.getElementById("f_age").value.trim();

                            if (!name || !gender || !age) return alert("Fill name, gender, and age.");

                            patients[editingPatientId] = { ...patients[editingPatientId], name, gender, age };
                            savePatientsToStorage();
                            cancelEdit();
                            renderPatientsUI();
                        }

                        function activatePatient(id) {
                            if (sessionRunning) return alert("Stop the session before switching patients.");

                            const p = patients[id];
                            if (!p.name || !p.gender || !p.age) return alert("Set patient info first (Edit → Save).");

                            activePatientId = id;

                            // Update the live page label
                            const label = `${id === 0 ? "Patient A" : "Patient B"}: ${p.name}`;
                            document.getElementById('selectedPatientLabel').textContent = label;

>>>>>>> Stashed changes
                            historyStack.push(currentPage);
                            showPage('live');
                        }

<<<<<<< Updated upstream
=======
                        function lockPatientControls(locked) {
                            // Disable editing/selecting while session runs
                            document.getElementById("btnEditA").disabled = locked;
                            document.getElementById("btnSelectA").disabled = locked;
                            document.getElementById("btnEditB").disabled = locked;
                            document.getElementById("btnSelectB").disabled = locked;
                        }

                        // ==========================================================
                        // Reports storage (CSV saved in browser, shown in Reports tab)
                        //
                        // Each report object:
                        // { id, filename, createdAtIso, patientLabel, csvText, sampleCount }
                        //
                        // MVP: store as localStorage JSON.
                        // ==========================================================
                        const REPORTS_KEY = "vitals_reports_v1";
                        let reports = [];

                        function loadReports() {
                            try {
                                const raw = localStorage.getItem(REPORTS_KEY);
                                if (!raw) { reports = []; return; }
                                const arr = JSON.parse(raw);
                                reports = Array.isArray(arr) ? arr : [];
                            } catch (e) {
                                reports = [];
                            }
                        }

                        function saveReports() {
                            try {
                                localStorage.setItem(REPORTS_KEY, JSON.stringify(reports));
                            } catch (e) {
                                alert("Storage is full. Delete some reports and try again.");
                            }
                        }

                        // This function DOES download; we only call it when user clicks Download in Reports.
                        function downloadTextFile(filename, text) {
                            const blob = new Blob([text], { type: "text/csv;charset=utf-8" });
                            const url = URL.createObjectURL(blob);

                            const a = document.createElement("a");
                            a.href = url;
                            a.download = filename;

                            document.body.appendChild(a);
                            a.click();
                            a.remove();

                            URL.revokeObjectURL(url);
                        }

                        function renderReports() {
                            const list = document.getElementById("reportsList");
                            if (!list) return;

                            if (!reports.length) {
                                list.innerHTML = `<div class="muted">No saved reports yet.</div>`;
                                return;
                            }

                            // Show newest first
                            const sorted = [...reports].sort((a,b) =>
                                (b.createdAtIso || "").localeCompare(a.createdAtIso || "")
                            );

                            list.innerHTML = sorted.map(r => `
                                <div class="list-item">
                                    <div style="display:flex; justify-content:space-between; align-items:center; gap:12px;">
                                        <div>
                                            <div><b>${r.filename}</b></div>
                                            <div class="muted">
                                                ${r.patientLabel} • ${new Date(r.createdAtIso).toLocaleString()} • ${r.sampleCount} samples
                                            </div>
                                        </div>
                                        <div style="display:flex; gap:8px;">
                                            <button class="mini-btn" onclick="downloadReport('${r.id}')">Download</button>
                                            <button class="mini-btn" onclick="deleteReport('${r.id}')">Delete</button>
                                        </div>
                                    </div>
                                </div>
                            `).join("");
                        }

                        function downloadReport(id) {
                            const r = reports.find(x => x.id === id);
                            if (!r) return;
                            downloadTextFile(r.filename, r.csvText);
                        }

                        function deleteReport(id) {
                            reports = reports.filter(x => x.id !== id);
                            saveReports();
                            renderReports();
                        }

                        function clearAllReports() {
                            if (!confirm("Clear all saved reports?")) return;
                            reports = [];
                            saveReports();
                            renderReports();
                        }

                        // ==========================================================
                        // Risk badge color for JS (matches C++ riskColor)
                        // ==========================================================
>>>>>>> Stashed changes
                        function riskColorJS(r) {
                            if (r === 0) return "#2ecc71";
                            if (r === 1) return "#f1c40f";
                            if (r === 2) return "#e67e22";
                            return "#e74c3c";
                        }

<<<<<<< Updated upstream
=======
                        // ==========================================================
                        // Live vitals polling (1Hz)
                        // - updates UI every second
                        // - session logger also uses /api/vitals snapshots
                        // ==========================================================
>>>>>>> Stashed changes
                        async function refreshVitals() {
                            try {
                                const r = await fetch('/api/vitals');
                                const v = await r.json();

<<<<<<< Updated upstream
=======
                                // Update main vital readouts
>>>>>>> Stashed changes
                                document.getElementById('hrValue').textContent   = Math.round(v.hr) + " bpm";
                                document.getElementById('spo2Value').textContent = Math.round(v.spo2) + " %";
                                document.getElementById('tempValue').textContent = Number(v.temp).toFixed(1) + " °C";
                                document.getElementById('respValue').textContent = Math.round(v.resp) + " rpm";
                                document.getElementById('bpValue').textContent   = Math.round(v.sys) + "/" + Math.round(v.dia) + " mmHg";

<<<<<<< Updated upstream
=======
                                // Update risk badges
>>>>>>> Stashed changes
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
<<<<<<< Updated upstream
                                // ignore for now
                            }
                        }

=======
                                // If server isn't ready or fetch fails, ignore for now.
                            }
                        }

                        // ==========================================================
                        // Clinical summary refresh (calls /api/summary once)
                        // ==========================================================
>>>>>>> Stashed changes
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

<<<<<<< Updated upstream
                        window.addEventListener('load', () => {
                            // start live vitals polling
                            refreshVitals();
                            setInterval(refreshVitals, 1000);

                            // generate summary on load (background)
=======
                        // ==========================================================
                        // 3-minute Vitals Session (1 Hz logging)
                        //
                        // Behavior:
                        // - requires active patient
                        // - locks patient controls
                        // - logs one snapshot per second for 180 seconds
                        // - builds CSV at end
                        // - SAVES CSV to Reports (localStorage)
                        // - user downloads later from Reports tab
                        // ==========================================================
                        const LOG_HZ = 1;
                        const SESSION_SECONDS = 180;

                        let sessionRunning = false;
                        let secondsLeft = SESSION_SECONDS;

                        let countdownTimer = null; // counts down seconds
                        let logTimer = null;       // triggers sampling
                        let sessionSamples = [];   // array of row objects

                        function pad2(n){ return String(n).padStart(2,'0'); }

                        function updateTimerUI() {
                            const m = Math.floor(secondsLeft / 60);
                            const s = secondsLeft % 60;
                            document.getElementById("timerLabel").textContent = `${m}:${pad2(s)}`;
                        }

                        // Fetch a single snapshot from backend and store it in sessionSamples[]
                        async function sampleVitalsOnce() {
                            const r = await fetch('/api/vitals');
                            const v = await r.json();

                            sessionSamples.push({
                                ts_iso: new Date().toISOString(),

                                hr: v.hr,
                                spo2: v.spo2,
                                temp: v.temp,
                                resp: v.resp,
                                sys: v.sys,
                                dia: v.dia,

                                risk_hr: v.risk_hr,
                                risk_spo2: v.risk_spo2,
                                risk_temp: v.risk_temp,
                                risk_resp: v.risk_resp,
                                risk_bp: v.risk_bp
                            });
                        }

                        // Build a CSV string with:
                        // - patient metadata at top
                        // - blank line
                        // - a header row + data rows
                        function buildCSV(patient, samples) {
                            const lines = [];

                            // Metadata block (helps TA see patient info is attached to session)
                            lines.push(`patient_name,${patient.name}`);
                            lines.push(`gender,${patient.gender}`);
                            lines.push(`age,${patient.age}`);
                            lines.push(`session_start,${samples[0]?.ts_iso || ""}`);
                            lines.push(`session_duration_s,${SESSION_SECONDS}`);
                            lines.push("");

                            // Table header
                            lines.push("timestamp_iso,HR_bpm,SpO2_pct,Temp_C,Resp_rpm,BP_sys,BP_dia,risk_hr,risk_spo2,risk_temp,risk_resp,risk_bp");

                            // Rows
                            for (const s of samples) {
                                lines.push([
                                    s.ts_iso,
                                    s.hr, s.spo2, s.temp, s.resp, s.sys, s.dia,
                                    s.risk_hr, s.risk_spo2, s.risk_temp, s.risk_resp, s.risk_bp
                                ].join(","));
                            }
                            return lines.join("\n");
                        }

                        function startVitalsSession() {
                            if (sessionRunning) return;

                            // Must have an active patient selected
                            if (activePatientId === null) return alert("Select a patient first.");

                            const p = patients[activePatientId];
                            if (!p.name || !p.gender || !p.age) return alert("Complete patient profile first.");

                            // Lock state
                            sessionRunning = true;
                            secondsLeft = SESSION_SECONDS;
                            sessionSamples = [];
                            updateTimerUI();

                            // Lock UI: can't edit/switch patients during recording
                            lockPatientControls(true);

                            // Update session status
                            document.getElementById("startBtn").disabled = true;
                            document.getElementById("sessionStatus").textContent = "Recording…";

                            // Sample immediately, then every 1 second
                            sampleVitalsOnce().catch(()=>{});
                            logTimer = setInterval(() => sampleVitalsOnce().catch(()=>{}), 1000 / LOG_HZ);

                            // Countdown every second
                            countdownTimer = setInterval(() => {
                                secondsLeft--;
                                updateTimerUI();

                                if (secondsLeft <= 0) {
                                    stopVitalsSessionAndSaveReport();
                                }
                            }, 1000);
                        }

                        function stopVitalsSessionAndSaveReport() {
                            if (!sessionRunning) return;

                            // Stop timers
                            sessionRunning = false;
                            clearInterval(countdownTimer);
                            clearInterval(logTimer);
                            countdownTimer = null;
                            logTimer = null;

                            // Unlock UI
                            lockPatientControls(false);
                            document.getElementById("startBtn").disabled = false;

                            // If no samples, stop gracefully
                            if (sessionSamples.length === 0) {
                                document.getElementById("sessionStatus").textContent = "Stopped (no data).";
                                return;
                            }

                            // Build filename
                            const p = patients[activePatientId];
                            const now = new Date();
                            const stamp = `${now.getFullYear()}-${pad2(now.getMonth()+1)}-${pad2(now.getDate())}_${pad2(now.getHours())}-${pad2(now.getMinutes())}-${pad2(now.getSeconds())}`;
                            const safeName = p.name.replace(/[^a-z0-9]+/gi, "_");
                            const fname = `${activePatientId === 0 ? "PatientA" : "PatientB"}_${safeName}_${stamp}.csv`;

                            // Build CSV text
                            const csv = buildCSV(p, sessionSamples);

                            // Save report entry (instead of auto-downloading)
                            const reportId = "r_" + Math.random().toString(16).slice(2) + "_" + Date.now();

                            reports.push({
                                id: reportId,
                                filename: fname,
                                createdAtIso: now.toISOString(),
                                patientLabel: `${activePatientId === 0 ? "Patient A" : "Patient B"}: ${p.name}`,
                                csvText: csv,
                                sampleCount: sessionSamples.length
                            });

                            saveReports();
                            renderReports();

                            // Update UI status (now it’s “Saved”, not “Exported”)
                            document.getElementById("sessionStatus").textContent = "Complete. Saved to Reports.";

                            // Update "last export" label on patient list
                            patients[activePatientId].lastExport = `${now.toLocaleDateString()} ${now.toLocaleTimeString()}`;
                            savePatientsToStorage();
                            renderPatientsUI();
                        }

                        // ==========================================================
                        // Page initialization on load
                        // ==========================================================
                        window.addEventListener('load', () => {
                            // Load stored patient profiles + reports
                            loadPatients();
                            renderPatientsUI();

                            loadReports();
                            renderReports();

                            updateTimerUI();

                            // Start live vitals polling (1Hz)
                            refreshVitals();
                            setInterval(refreshVitals, 1000);

                            // Generate summary in background on load
>>>>>>> Stashed changes
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
