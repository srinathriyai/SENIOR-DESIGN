#include "WebServer.h"          // startWebServer() + Vitals struct + risk funcs
#include "LLM.h"                // sendToLLM(), extractContent()
#include <iostream>
#include <map>
#include <string>
#include <sstream>
#include <cctype>
#include <cmath>

// ============================================================================
// WebServer.cpp (Clinical Diagnosis AFTER 3-minute session)
//
// What this file does:
// 1) Hosts a local web dashboard at: http://localhost:8000
// 2) Exposes API endpoints the dashboard uses:
//    - GET  /api/vitals     -> latest vitals + risk levels (JSON) [polled at 1 Hz]
//    - POST /api/diagnosis  -> generate ONE diagnosis from averaged session data
// 3) Serves a single-page dashboard UI from C++ (HTML + JS embedded)
//
// New behavior (what you asked for):
// - RAW data logging stays untouched (sessionSamples[] + CSV save under Reports)
// - Clinical diagnosis only generates AFTER the 3-minute session ends
// - Diagnosis is based on averages of the logged samples (same ones used in CSV)
// - Diagnosis is stored under its own tab "Clinical Diagnosis" (localStorage)
// ============================================================================


// ---- Live vitals source (set by pc_main.cpp) ----
// If pc_main.cpp calls setLiveVitalsSource(&liveVitals),
// the server will always serve the latest values from that struct.
static const Vitals* g_liveVitals = nullptr;

void setLiveVitalsSource(const Vitals* live) {
    g_liveVitals = live;
}

// -------------------------------
// Convert risk score (0–3) → color hex code
// Used for UI badges.
// -------------------------------
static std::string riskColor(int risk) {
    switch (risk) {
        case 0: return "#2ecc71"; // green = normal
        case 1: return "#f1c40f"; // yellow = mild
        case 2: return "#e67e22"; // orange = moderate
        case 3: return "#e74c3c"; // red = severe
        default: return "#bdc3c7"; // grey fallback
    }
}

// -------------------------------
// Escape text so it can be safely injected into HTML
// -------------------------------
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

// ============================================================================
// SUPER SMALL JSON PARSER HELPERS (for MVP)
// We only need to parse simple JSON like:
// {
//   "hr":72, "spo2":98, "temp":36.8, "resp":14, "sys":120, "dia":80,
//   "risk_hr":1, ...,
//   "name":"Jane", "age":21, "gender":"Female", "visit":"2026-01-31 ..."
// }
// ============================================================================

// Find key in JSON: returns index of first char after ':', or npos.
static size_t findJsonValueStart(const std::string& body, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t k = body.find(needle);
    if (k == std::string::npos) return std::string::npos;

    size_t colon = body.find(':', k + needle.size());
    if (colon == std::string::npos) return std::string::npos;

    size_t i = colon + 1;
    while (i < body.size() && std::isspace((unsigned char)body[i])) i++;
    return i;
}

static double jsonGetNumber(const std::string& body, const std::string& key, double fallback = 0.0) {
    size_t i = findJsonValueStart(body, key);
    if (i == std::string::npos) return fallback;

    // parse optional sign + digits + optional decimal
    size_t j = i;
    if (j < body.size() && (body[j] == '-' || body[j] == '+')) j++;

    bool sawDigit = false;
    while (j < body.size() && std::isdigit((unsigned char)body[j])) { j++; sawDigit = true; }

    if (j < body.size() && body[j] == '.') {
        j++;
        while (j < body.size() && std::isdigit((unsigned char)body[j])) { j++; sawDigit = true; }
    }

    if (!sawDigit) return fallback;

    try {
        return std::stod(body.substr(i, j - i));
    } catch (...) {
        return fallback;
    }
}

static int jsonGetInt(const std::string& body, const std::string& key, int fallback = 0) {
    return (int)std::lround(jsonGetNumber(body, key, (double)fallback));
}

static std::string jsonGetString(const std::string& body, const std::string& key, const std::string& fallback = "") {
    size_t i = findJsonValueStart(body, key);
    if (i == std::string::npos) return fallback;

    // must start with quote
    if (i >= body.size() || body[i] != '"') return fallback;
    i++; // after opening quote

    std::string out;
    while (i < body.size()) {
        char c = body[i++];
        if (c == '"') break; // end
        if (c == '\\' && i < body.size()) {
            // minimal escape support
            char n = body[i++];
            if (n == 'n') out.push_back('\n');
            else if (n == 't') out.push_back('\t');
            else out.push_back(n);
        } else {
            out.push_back(c);
        }
    }
    return out.empty() ? fallback : out;
}


// ====================================================================
// MAIN UI SERVER FUNCTION
// ====================================================================
void startWebServer(const Vitals& current, const std::string& /*llmResponse*/) {

    httplib::Server svr;

    // =========================================================
    // ROUTE: GET /api/vitals
    // Returns latest vitals + risk as JSON (for live UI updates)
    //
    // Frontend calls this once per second (1 Hz).
    // =========================================================
    svr.Get("/api/vitals", [&](const httplib::Request&, httplib::Response& res) {

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
    // ROUTE: POST /api/diagnosis
    //
    // Called ONCE after a 3-minute session ends.
    // The browser sends averaged vitals + averaged risk levels + patient metadata.
    // Server calls llama and returns plain text diagnosis.
    // =========================================================
    svr.Post("/api/diagnosis", [&](const httplib::Request& req, httplib::Response& res) {

        const std::string& body = req.body;

        // Pull averaged vitals
        Vitals v{};
        v.HR     = (float)jsonGetNumber(body, "hr", 0);
        v.SpO2   = (float)jsonGetNumber(body, "spo2", 0);
        v.Temp   = (float)jsonGetNumber(body, "temp", 0);
        v.Resp   = (float)jsonGetNumber(body, "resp", 0);
        v.BP_sys = (float)jsonGetNumber(body, "sys", 0);
        v.BP_dia = (float)jsonGetNumber(body, "dia", 0);

        // Pull averaged risk (0–3)
        int r_hr   = jsonGetInt(body, "risk_hr", 0);
        int r_spo2 = jsonGetInt(body, "risk_spo2", 0);
        int r_temp = jsonGetInt(body, "risk_temp", 0);
        int r_resp = jsonGetInt(body, "risk_resp", 0);
        int r_bp   = jsonGetInt(body, "risk_bp", 0);

        // Patient metadata (strings)
        std::string name   = jsonGetString(body, "name", "Unknown");
        std::string gender = jsonGetString(body, "gender", "Unknown");
        int age            = jsonGetInt(body, "age", 0);
        std::string visit  = jsonGetString(body, "visit", "Unknown");

        // Build prompt that forces your exact template formatting.
        // IMPORTANT: This prompt uses averaged data (the session summary), not instantaneous vitals.
        std::ostringstream prompt;
        prompt
        << "You are a clinical assistant generating a structured summary for a vitals monitoring session.\n"
        << "Use ONLY the provided averaged session data. Do NOT invent numbers.\n"
        << "Return output EXACTLY in the following template and keep headings exactly as shown:\n\n"
        << "Name:\n"
        << "Age:\n"
        << "Gender:\n"
        << "Date & Time of Visit:\n\n"
        << "Patient Vitals: (Averaged data)\n\n"
        << "HR:\n"
        << "SpO2:\n"
        << "Temperature:\n"
        << "Respiratory Rate:\n"
        << "BP:\n\n"
        << "Risk Level: (averaged)\n\n"
        << "**Disclaimer: All diagnoses are rendered from an AI, it does not constitute professional medical advice**\n\n"
        << "Current status of Diagnosis:\n\n"
        << "Treatment/Goal Plan:\n\n"
        << "----\n"
        << "PATIENT DATA (AVERAGED OVER 3 MINUTES):\n"
        << "Name=" << name << "\n"
        << "Age=" << age << "\n"
        << "Gender=" << gender << "\n"
        << "Visit=" << visit << "\n"
        << "HR_bpm=" << v.HR << "\n"
        << "SpO2_pct=" << v.SpO2 << "\n"
        << "Temp_C=" << v.Temp << "\n"
        << "Resp_rpm=" << v.Resp << "\n"
        << "BP_sys=" << v.BP_sys << "\n"
        << "BP_dia=" << v.BP_dia << "\n"
        << "Risk_HR=" << r_hr << "\n"
        << "Risk_SpO2=" << r_spo2 << "\n"
        << "Risk_Temp=" << r_temp << "\n"
        << "Risk_Resp=" << r_resp << "\n"
        << "Risk_BP=" << r_bp << "\n";

        // Call llama-server through your existing helper
        std::string rawJson = sendToLLM(prompt.str());
        std::string diagnosis = extractContent(rawJson);

        res.set_content(diagnosis, "text/plain; charset=utf-8");
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

                .app { display: flex; min-height: 100vh; }

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
                }

                .container { max-width: 900px; margin: 0; }

                h2 { text-align: left; margin: 0 0 8px 0; font-weight: 700; }

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

                .vital-name { font-size: 16px; font-weight: 600; }

                .vital-value { font-size: 32px; font-weight: 700; margin-top: 5px; }

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

                .mini-btn {
                    padding: 8px 10px;
                    border-radius: 10px;
                    border: 1px solid rgba(0,0,0,0.12);
                    background: #fff;
                    cursor: pointer;
                    font-size: 13px;
                }
                .mini-btn:disabled { opacity: 0.5; cursor: not-allowed; }

                .field {
                    width: 100%;
                    padding: 10px;
                    border-radius: 10px;
                    border: 1px solid rgba(0,0,0,0.12);
                    box-sizing: border-box;
                }
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
                        <button id="navDx" onclick="go('diagnosis')">Clinical Diagnosis</button>
                    </div>

                    <div style="margin-top:16px;" class="muted">
                        Run a 3-minute session to generate a diagnosis from averaged session data.
                    </div>
                </aside>

                <main class="content">
                    <div class="topbar">
                        <button class="back-btn" onclick="back()">← Back</button>
                        <h1 id="pageTitle" class="page-title">Patients</h1>
                    </div>

                    <!-- ==========================================================
                         PAGE: PATIENTS
                         ========================================================== -->
                    <section id="pagePatients" class="page active">
                        <div class="card">
                            <div class="vital-name">Patient Profiles (max 2)</div>
                            <div class="muted" style="margin-top:6px;">
                                Edit a profile, then select it before starting a vitals session.
                            </div>

                            <div class="list">
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
                                </div>
                            </div>
                        </div>
                    </section>

                    <!-- ==========================================================
                         PAGE: LIVE
                         ========================================================== -->
                    <section id="pageLive" class="page">
                        <div class="container">
                            <h2>Live Vitals</h2>
                            <div class="muted" style="margin-bottom:12px;">
                                Active patient: <span id="selectedPatientLabel">None</span>
                            </div>

                            <div class="card" style="margin-bottom:14px;">
                                <div class="vital-name">Vitals Session</div>
                                <div class="muted" style="margin-top:6px;">
                                    Logs 1 sample/sec for 3 minutes, then saves a CSV under Reports and generates diagnosis.
                                </div>

                                <div style="display:flex; align-items:center; gap:12px; margin-top:12px;">
                                    <button id="startBtn" class="mini-btn" onclick="startVitalsSession()">
                                        Start Vitals (3:00)
                                    </button>
                                    <div class="muted">Time left: <span id="timerLabel">3:00</span></div>
                                    <div class="muted" id="sessionStatus"></div>
                                </div>
                            </div>

                            <div class="grid">
        )HTML";

        // initial cards
        html += "<div class='card'><div class='vital-name'>Heart Rate</div>";
        html += "<div id='hrValue' class='vital-value'>" + std::to_string((int)current.HR) + " bpm</div>";
        html += "<div id='hrRisk' class='badge' style='background:" + riskColor(risk["HR"]) + "'>Risk " + std::to_string(risk["HR"]) + "</div></div>";

        html += "<div class='card'><div class='vital-name'>SpO₂</div>";
        html += "<div id='spo2Value' class='vital-value'>" + std::to_string((int)current.SpO2) + " %</div>";
        html += "<div id='spo2Risk' class='badge' style='background:" + riskColor(risk["SpO2"]) + "'>Risk " + std::to_string(risk["SpO2"]) + "</div></div>";

        html += "<div class='card'><div class='vital-name'>Temperature</div>";
        html += "<div id='tempValue' class='vital-value'>" + std::to_string(current.Temp) + " °C</div>";
        html += "<div id='tempRisk' class='badge' style='background:" + riskColor(risk["Temp"]) + "'>Risk " + std::to_string(risk["Temp"]) + "</div></div>";

        html += "<div class='card'><div class='vital-name'>Respiratory Rate</div>";
        html += "<div id='respValue' class='vital-value'>" + std::to_string((int)current.Resp) + " rpm</div>";
        html += "<div id='respRisk' class='badge' style='background:" + riskColor(risk["Resp"]) + "'>Risk " + std::to_string(risk["Resp"]) + "</div></div>";

        html += "<div class='card'><div class='vital-name'>Blood Pressure</div>";
        html += "<div id='bpValue' class='vital-value'>" + std::to_string((int)current.BP_sys)
             + "/" + std::to_string((int)current.BP_dia) + " mmHg</div>";
        html += "<div id='bpRisk' class='badge' style='background:" + riskColor(risk["BP"]) + "'>Risk "
             + std::to_string(risk["BP"]) + "</div></div>";

        // Diagnosis box on Live page (not generated until session ends)
        html += R"HTML(
                            </div>

                            <div class="card summary-card">
                                <div class="vital-name">Clinical Diagnosis (after session)</div>
                                <div id="dxText" class="summary-content">Run a 3-minute session to generate diagnosis.</div>
                                <div id="dxStatus" class="muted" style="margin-top:10px;"></div>
                            </div>
                        </div>
                    </section>

                    <!-- ==========================================================
                         PAGE: REPORTS
                         ========================================================== -->
                    <section id="pageReports" class="page">
                        <div class="card">
                            <div class="vital-name">Reports</div>
                            <div class="muted">Saved session exports. Download anytime.</div>

                            <div class="list" id="reportsList" style="margin-top:14px;"></div>

                            <div style="margin-top:12px;">
                                <button class="mini-btn" onclick="clearAllReports()">Clear all</button>
                            </div>
                        </div>
                    </section>

                    <!-- ==========================================================
                         PAGE: CLINICAL DIAGNOSIS
                         ========================================================== -->
                    <section id="pageDx" class="page">
                        <div class="card">
                            <div class="vital-name">Clinical Diagnosis</div>
                            <div class="muted">Generated once per completed 3-minute session.</div>
                            <div class="list" id="dxList" style="margin-top:14px;"></div>

                            <div style="margin-top:12px;">
                                <button class="mini-btn" onclick="clearAllDx()">Clear all</button>
                            </div>
                        </div>
                    </section>

                    <script>
                        // ==========================================================
                        // Navigation
                        // ==========================================================
                        const historyStack = [];
                        let currentPage = 'patients';

                        function setActiveNav(page) {
                            document.getElementById('navPatients').classList.toggle('active', page === 'patients');
                            document.getElementById('navLive').classList.toggle('active', page === 'live');
                            document.getElementById('navReports').classList.toggle('active', page === 'reports');
                            document.getElementById('navDx').classList.toggle('active', page === 'diagnosis');
                        }

                        function setPageTitle(page) {
                            const title =
                                (page === 'patients') ? 'Patients' :
                                (page === 'live') ? 'Live Vitals' :
                                (page === 'reports') ? 'Reports' :
                                'Clinical Diagnosis';
                            document.getElementById('pageTitle').textContent = title;
                        }

                        function showPage(page) {
                            document.getElementById('pagePatients').classList.toggle('active', page === 'patients');
                            document.getElementById('pageLive').classList.toggle('active', page === 'live');
                            document.getElementById('pageReports').classList.toggle('active', page === 'reports');
                            document.getElementById('pageDx').classList.toggle('active', page === 'diagnosis');

                            currentPage = page;
                            setActiveNav(page);
                            setPageTitle(page);

                            if (page === 'reports') renderReports();
                            if (page === 'diagnosis') renderDx();
                        }

                        function go(page) {
                            if (page === currentPage) return;
                            historyStack.push(currentPage);
                            showPage(page);
                        }

                        function back() {
                            if (historyStack.length === 0) return;
                            showPage(historyStack.pop());
                        }

                        // ==========================================================
                        // Patient profiles (localStorage)
                        // ==========================================================
                        const LS_KEY = "vitals_patients_v1";
                        let patients = [
                            { name: "", gender: "", age: "", lastExport: "" },
                            { name: "", gender: "", age: "", lastExport: "" }
                        ];

                        let activePatientId = null;
                        let editingPatientId = null;

                        function loadPatients() {
                            try {
                                const raw = localStorage.getItem(LS_KEY);
                                if (!raw) return;
                                const obj = JSON.parse(raw);
                                if (Array.isArray(obj) && obj.length === 2) patients = obj;
                            } catch (e) {}
                        }

                        function savePatientsToStorage() {
                            try { localStorage.setItem(LS_KEY, JSON.stringify(patients)); }
                            catch (e) {}
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
                            const label = `${id === 0 ? "Patient A" : "Patient B"}: ${p.name}`;
                            document.getElementById('selectedPatientLabel').textContent = label;
                            historyStack.push(currentPage);
                            showPage('live');
                        }

                        function lockPatientControls(locked) {
                            document.getElementById("btnEditA").disabled = locked;
                            document.getElementById("btnSelectA").disabled = locked;
                            document.getElementById("btnEditB").disabled = locked;
                            document.getElementById("btnSelectB").disabled = locked;
                        }

                        // ==========================================================
                        // Reports storage (CSV) - unchanged behavior
                        // ==========================================================
                        const REPORTS_KEY = "vitals_reports_v1";
                        let reports = [];

                        function loadReports() {
                            try {
                                const raw = localStorage.getItem(REPORTS_KEY);
                                if (!raw) { reports = []; return; }
                                const arr = JSON.parse(raw);
                                reports = Array.isArray(arr) ? arr : [];
                            } catch (e) { reports = []; }
                        }

                        function saveReports() {
                            try { localStorage.setItem(REPORTS_KEY, JSON.stringify(reports)); }
                            catch (e) { alert("Storage is full. Delete some reports and try again."); }
                        }

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
                        // Clinical Diagnosis storage (separate tab)
                        // ==========================================================
                        const DX_KEY = "vitals_dx_v1";
                        let dxItems = []; // {id, createdAtIso, patientLabel, text, reportId}

                        function loadDx() {
                            try {
                                const raw = localStorage.getItem(DX_KEY);
                                if (!raw) { dxItems = []; return; }
                                const arr = JSON.parse(raw);
                                dxItems = Array.isArray(arr) ? arr : [];
                            } catch (e) { dxItems = []; }
                        }

                        function saveDx() {
                            try { localStorage.setItem(DX_KEY, JSON.stringify(dxItems)); }
                            catch (e) { alert("Storage is full. Delete some diagnoses and try again."); }
                        }

                        function renderDx() {
                            const list = document.getElementById("dxList");
                            if (!list) return;

                            if (!dxItems.length) {
                                list.innerHTML = `<div class="muted">No diagnoses generated yet.</div>`;
                                return;
                            }

                            const sorted = [...dxItems].sort((a,b) =>
                                (b.createdAtIso || "").localeCompare(a.createdAtIso || "")
                            );

                            list.innerHTML = sorted.map(d => `
                                <div class="list-item">
                                    <div style="display:flex; justify-content:space-between; align-items:flex-start; gap:12px;">
                                        <div style="flex:1;">
                                            <div><b>${d.patientLabel}</b></div>
                                            <div class="muted">${new Date(d.createdAtIso).toLocaleString()}</div>
                                            <div style="margin-top:10px; white-space:pre-wrap; line-height:1.5;">${escapeHtml(d.text)}</div>
                                        </div>
                                        <div style="display:flex; gap:8px;">
                                            <button class="mini-btn" onclick="deleteDx('${d.id}')">Delete</button>
                                        </div>
                                    </div>
                                </div>
                            `).join("");
                        }

                        function deleteDx(id) {
                            dxItems = dxItems.filter(x => x.id !== id);
                            saveDx();
                            renderDx();
                        }

                        function clearAllDx() {
                            if (!confirm("Clear all diagnoses?")) return;
                            dxItems = [];
                            saveDx();
                            renderDx();
                        }

                        // Safe-ish HTML escaping for showing dx text inside list items
                        function escapeHtml(s) {
                            return String(s)
                                .replaceAll("&","&amp;")
                                .replaceAll("<","&lt;")
                                .replaceAll(">","&gt;")
                                .replaceAll('"',"&quot;")
                                .replaceAll("'","&#39;");
                        }

                        // ==========================================================
                        // Risk badge color (matches C++)
                        // ==========================================================
                        function riskColorJS(r) {
                            if (r === 0) return "#2ecc71";
                            if (r === 1) return "#f1c40f";
                            if (r === 2) return "#e67e22";
                            return "#e74c3c";
                        }

                        // ==========================================================
                        // Live vitals polling (1 Hz)
                        // ==========================================================
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
                            } catch (e) {}
                        }

                        // ==========================================================
                        // 3-minute Session Logging
                        // ==========================================================
                        const LOG_HZ = 1;
                        const SESSION_SECONDS = 180;

                        let sessionRunning = false;
                        let secondsLeft = SESSION_SECONDS;

                        let countdownTimer = null;
                        let logTimer = null;
                        let sessionSamples = [];

                        function pad2(n){ return String(n).padStart(2,'0'); }

                        function updateTimerUI() {
                            const m = Math.floor(secondsLeft / 60);
                            const s = secondsLeft % 60;
                            document.getElementById("timerLabel").textContent = `${m}:${pad2(s)}`;
                        }

                        async function sampleVitalsOnce() {
                            const r = await fetch('/api/vitals');
                            const v = await r.json();

                            sessionSamples.push({
                                ts_iso: new Date().toISOString(),
                                hr: v.hr, spo2: v.spo2, temp: v.temp, resp: v.resp, sys: v.sys, dia: v.dia,
                                risk_hr: v.risk_hr, risk_spo2: v.risk_spo2, risk_temp: v.risk_temp,
                                risk_resp: v.risk_resp, risk_bp: v.risk_bp
                            });
                        }

                        function buildCSV(patient, samples) {
                            const lines = [];
                            lines.push(`patient_name,${patient.name}`);
                            lines.push(`gender,${patient.gender}`);
                            lines.push(`age,${patient.age}`);
                            lines.push(`session_start,${samples[0]?.ts_iso || ""}`);
                            lines.push(`session_duration_s,${SESSION_SECONDS}`);
                            lines.push("");

                            lines.push("timestamp_iso,HR_bpm,SpO2_pct,Temp_C,Resp_rpm,BP_sys,BP_dia,risk_hr,risk_spo2,risk_temp,risk_resp,risk_bp");

                            for (const s of samples) {
                                lines.push([
                                    s.ts_iso,
                                    s.hr, s.spo2, s.temp, s.resp, s.sys, s.dia,
                                    s.risk_hr, s.risk_spo2, s.risk_temp, s.risk_resp, s.risk_bp
                                ].join(","));
                            }
                            return lines.join("\n");
                        }

                        // Compute averages from the session samples (these are the values we send to LLM)
                        function computeAverages(samples) {
                            const n = samples.length || 1;

                            const mean = (arr, key) => arr.reduce((a,x)=>a + Number(x[key]||0), 0) / n;

                            const avg = {
                                hr:   mean(samples, "hr"),
                                spo2: mean(samples, "spo2"),
                                temp: mean(samples, "temp"),
                                resp: mean(samples, "resp"),
                                sys:  mean(samples, "sys"),
                                dia:  mean(samples, "dia"),
                                // averaged risk: mean then round to nearest int 0–3
                                risk_hr:   Math.round(mean(samples, "risk_hr")),
                                risk_spo2: Math.round(mean(samples, "risk_spo2")),
                                risk_temp: Math.round(mean(samples, "risk_temp")),
                                risk_resp: Math.round(mean(samples, "risk_resp")),
                                risk_bp:   Math.round(mean(samples, "risk_bp"))
                            };
                            return avg;
                        }

                        async function generateDiagnosisFromSession(reportId, patient, samples) {
                            const dxTextEl = document.getElementById("dxText");
                            const dxStatusEl = document.getElementById("dxStatus");

                            if (dxStatusEl) dxStatusEl.textContent = "Generating clinical diagnosis…";
                            if (dxTextEl) dxTextEl.textContent = "Generating…";

                            const avg = computeAverages(samples);
                            const visit = new Date().toLocaleString();

                            // Payload sent to server endpoint /api/diagnosis
                            const payload = {
                                name: patient.name,
                                age: Number(patient.age),
                                gender: patient.gender,
                                visit: visit,

                                hr: avg.hr,
                                spo2: avg.spo2,
                                temp: avg.temp,
                                resp: avg.resp,
                                sys: avg.sys,
                                dia: avg.dia,

                                risk_hr: avg.risk_hr,
                                risk_spo2: avg.risk_spo2,
                                risk_temp: avg.risk_temp,
                                risk_resp: avg.risk_resp,
                                risk_bp: avg.risk_bp
                            };

                            try {
                                const r = await fetch("/api/diagnosis", {
                                    method: "POST",
                                    headers: { "Content-Type": "application/json" },
                                    body: JSON.stringify(payload)
                                });

                                const text = await r.text();

                                // Show on Live page
                                if (dxTextEl) dxTextEl.textContent = text;
                                if (dxStatusEl) dxStatusEl.textContent = "";

                                // Save into Diagnosis tab
                                const id = "dx_" + Math.random().toString(16).slice(2) + "_" + Date.now();
                                const nowIso = new Date().toISOString();
                                const patientLabel = `${activePatientId === 0 ? "Patient A" : "Patient B"}: ${patient.name}`;

                                dxItems.push({
                                    id,
                                    createdAtIso: nowIso,
                                    patientLabel,
                                    text,
                                    reportId
                                });

                                saveDx();
                                renderDx();

                            } catch (e) {
                                if (dxStatusEl) dxStatusEl.textContent = "Diagnosis generation failed.";
                                if (dxTextEl) dxTextEl.textContent = "Run another session and try again.";
                            }
                        }

                        function startVitalsSession() {
                            if (sessionRunning) return;

                            if (activePatientId === null) return alert("Select a patient first.");
                            const p = patients[activePatientId];
                            if (!p.name || !p.gender || !p.age) return alert("Complete patient profile first.");

                            sessionRunning = true;
                            secondsLeft = SESSION_SECONDS;
                            sessionSamples = [];
                            updateTimerUI();

                            lockPatientControls(true);

                            document.getElementById("startBtn").disabled = true;
                            document.getElementById("sessionStatus").textContent = "Recording…";

                            // Clear live diagnosis display (so it doesn't look like it updated mid-session)
                            document.getElementById("dxStatus").textContent = "";
                            document.getElementById("dxText").textContent = "Recording session… diagnosis will generate after 3 minutes.";

                            sampleVitalsOnce().catch(()=>{});
                            logTimer = setInterval(() => sampleVitalsOnce().catch(()=>{}), 1000 / LOG_HZ);

                            countdownTimer = setInterval(() => {
                                secondsLeft--;
                                updateTimerUI();
                                if (secondsLeft <= 0) stopVitalsSessionAndSaveReport();
                            }, 1000);
                        }

                        function stopVitalsSessionAndSaveReport() {
                            if (!sessionRunning) return;

                            sessionRunning = false;
                            clearInterval(countdownTimer);
                            clearInterval(logTimer);
                            countdownTimer = null;
                            logTimer = null;

                            lockPatientControls(false);
                            document.getElementById("startBtn").disabled = false;

                            if (sessionSamples.length === 0) {
                                document.getElementById("sessionStatus").textContent = "Stopped (no data).";
                                return;
                            }

                            const p = patients[activePatientId];
                            const now = new Date();
                            const stamp = `${now.getFullYear()}-${pad2(now.getMonth()+1)}-${pad2(now.getDate())}_${pad2(now.getHours())}-${pad2(now.getMinutes())}-${pad2(now.getSeconds())}`;
                            const safeName = p.name.replace(/[^a-z0-9]+/gi, "_");
                            const fname = `${activePatientId === 0 ? "PatientA" : "PatientB"}_${safeName}_${stamp}.csv`;

                            const csv = buildCSV(p, sessionSamples);

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

                            document.getElementById("sessionStatus").textContent = "Complete. Saved to Reports. Generating diagnosis…";

                            patients[activePatientId].lastExport = `${now.toLocaleDateString()} ${now.toLocaleTimeString()}`;
                            savePatientsToStorage();
                            renderPatientsUI();

                            // NOW generate diagnosis ONCE using the samples we just collected
                            generateDiagnosisFromSession(reportId, p, sessionSamples)
                                .then(() => {
                                    document.getElementById("sessionStatus").textContent = "Complete. Saved to Reports + Diagnosis.";
                                })
                                .catch(() => {
                                    document.getElementById("sessionStatus").textContent = "Complete. Saved to Reports. Diagnosis failed.";
                                });
                        }

                        // ==========================================================
                        // Init
                        // ==========================================================
                        window.addEventListener('load', () => {
                            loadPatients();
                            renderPatientsUI();

                            loadReports();
                            renderReports();

                            loadDx();
                            renderDx();

                            updateTimerUI();

                            refreshVitals();
                            setInterval(refreshVitals, 1000);
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
