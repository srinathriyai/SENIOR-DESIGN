// FILE NAME: WebServer.cpp
#include "WebServer.h"     // startWebServer() + Vitals struct + risk funcs
#include "LLM.h"           // buildClinicalDiagnosisPrompt(), sendToLLMChat()
#include <iostream>
#include <map>
#include <string>
#include <sstream>
#include <cctype>
#include <cmath>

#include <mutex>
#include <chrono>

#include <atomic>
std::atomic<bool> g_liveEnabled{true}; // global flag to enable/disable live vitals updates (for testing)
std::atomic<bool> g_senseEnabled{false};   // HR/TEMP/RESP
std::atomic<bool> g_bpStart{false};        // one-shot trigger

static std::mutex g_vitalsMutex;
static Vitals g_latestFromWifi{};
static bool g_hasWifiVitals = false;
static std::chrono::steady_clock::time_point g_lastRx;




// ============================================================================
// WebServer.cpp (Clinical Diagnosis AFTER 3-minute session)
//
// Endpoints:
//   GET  /api/vitals     -> live vitals + risks (JSON) [polled at 1 Hz]
//   POST /api/diagnosis  -> ONE diagnosis from averaged session data (JSON body)
//
// UI:
//   - Patient Profiles tab
//   - Live Vitals tab (3-min session -> saves data in a CSV)
//   - Reports tab (CSV downloads)
//   - Clinical Diagnosis tab (stores diagnosis output per session)
//
// Key behavior:
//   - Raw session sampling + CSV saving is unchanged
//   - Clinical diagnosis ONLY runs after session completes
//   - Diagnosis is based on averages computed from the same sessionSamples[]
// ============================================================================

// ---- Live vitals source (set by pc_main.cpp) ----
static const Vitals* g_liveVitals = nullptr;

void setLiveVitalsSource(const Vitals* live) {
    g_liveVitals = live;
}

// Convert risk score (0–3) → color hex
static std::string riskColor(int risk) {
    switch (risk) {
        case 0: return "#2ecc71";   // No risk
        case 1: return "#f1c40f";   // Mild risk
        case 2: return "#e67e22";   // Moderate risk
        case 3: return "#e74c3c";   // High/Severe risk
        
        default: return "#bdc3c7";  
    }
}

// Escape text for HTML rendering
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
// Tiny JSON helpers (MVP)
// Supports extracting numbers + strings from a simple JSON object
// ============================================================================

/*
~~~~~~  findJsonValueStart ~~~~~~
** Parameters:
    - body: the full JSON response string
    - key: the key name to search for (e.g., "content")

** What this function does: 
    This function searches a JSON response string for a specified
    key and returns the index of that key's corresponding value starts

** How it works:
    1. It searches for the quoted key name (e.g., "content") within the JSON text
    2. Once the key is found, it locates the colon (:) that separates the key from its value
    3. It then skips any whitespace characters following the colon to find the start of the value
    4. Finally, it returns the index of the first character of the value

** What the return value means:
    - If the key is found, it returns the index of the first character of the value
    - If the key is not found, it returns std::string::npos
*/

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

/*
~~~~~~  jsonGetNumber ~~~~~~
** Parameters:
    - body: the full JSON response string
    - key: the key name whose numeric value is requested
    - fallback: default value returned if extraction or conversion fails

** What this function does:
    This function extracts a numeric value associated with a given
    key from a JSON-formatted string and converts it to a double.

** How it works:
    1. It calls findJsonValueStart() to locate where the value for the given key begins
    2. Starting from that position, it scans forward to identify a valid numeric pattern
       (optional sign, digits, and optional decimal point)
    3. It extracts the substring representing the number
    4. The substring is converted to a double using std::stod()

** What the return value means:
    - If a valid number is found and successfully converted, the function returns
      the extracted double value
    - If the key is not found, the value is not numeric, or conversion fails,
      the function returns the fallback value
*/

static double jsonGetNumber(const std::string& body, const std::string& key, double fallback = 0.0) {
    size_t i = findJsonValueStart(body, key);
    if (i == std::string::npos) return fallback;

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

/*
~~~~~~  jsonGetInt ~~~~~~
** What this function does:
    - Retrieves a numeric value from a JSON response and returns it as an integer
    - Rounds the value to the nearest integer and returns a fallback if extraction fails
**What the return value means:
    - Returns the integer value associated with the specified 
      key in the JSON response
    - If extraction or conversion fails, returns the provided 
      fallback integer value
*/

static int jsonGetInt(const std::string& body, const std::string& key, int fallback = 0) {
    return (int)std::lround(jsonGetNumber(body, key, (double)fallback));
}



/*
~~~~~~  jsonGetString ~~~~~~
** Parameters:
    - body: the full JSON response string
    - key: the key name whose string value is requested
    - fallback: default string returned if the extraction fails

** What this function does:
    This function extracts a string value associated with a given
    key from a JSON-formatted response string.

** How it works:
    1. It calls findJsonValueStart() to locate where the value for the given key begins
    2. It verifies that the value starts with a quotation mark, indicating a JSON string
    3. It reads characters until the closing quotation mark is reached
    4. Escape sequences (such as \\n and \\t) are decoded into their actual characters
    5. The extracted characters are accumulated into a C++ string

** What the return value means:
    - If a valid string is found, the function returns the decoded string value
    - If the key is not found, the value is not a string, or parsing fails,
      the function returns the fallback string
*/

static std::string jsonGetString(const std::string& body, const std::string& key, const std::string& fallback = "") {
    size_t i = findJsonValueStart(body, key);
    if (i == std::string::npos) return fallback;

    if (i >= body.size() || body[i] != '"') return fallback;
    i++;

    std::string out;
    while (i < body.size()) {
        char c = body[i++];
        if (c == '"') break;
        if (c == '\\' && i < body.size()) {
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

// This function is more flexible than jsonGetInt() 
// because it can handle both numeric and string 
// representations of numbers in the JSON (e.g., "age": 22 or "age": "22").

static int jsonGetIntFlexible(const std::string& body, const std::string& key, int fallback = 0) {
    // Try numeric age: 22
    int n = jsonGetInt(body, key, INT32_MIN);
    if (n != INT32_MIN) return n;

    // Try string age: "22"
    std::string s = jsonGetString(body, key, "");
    if (s.empty()) return fallback;

    try { return std::stoi(s); }
    catch (...) { return fallback; }
}
//2/19/26 --- Strip identity + any “Patient:” / “Visit:” / “Availabilities:” sections in code
static std::string stripLinePrefixes(const std::string& s) {
    std::istringstream in(s);
    std::ostringstream out;
    std::string line;

    auto startsWith = [](const std::string& a, const std::string& b) {
        return a.size() >= b.size() && a.compare(0, b.size(), b) == 0;
    };

    while (std::getline(in, line)) {
        // drop common identity/echo lines
        if (startsWith(line, "Patient:")) continue;
        if (startsWith(line, "Visit:")) continue;
        if (startsWith(line, "Name:")) continue;
        if (startsWith(line, "Age:")) continue;
        if (startsWith(line, "Gender:")) continue;
        if (startsWith(line, "Date & Time of Visit:")) continue;

        // drop “echo” sections some models produce
        if (startsWith(line, "Availabilities:")) continue;
        if (startsWith(line, "Risk assessment:")) continue;
        if (startsWith(line, "Risk:")) continue;

        out << line << "\n";
    }
    return out.str();
}
// This function can be used to extract the content of a specific tag 
// from the model's output.

static std::string extractTag(const std::string& s, const std::string& tag) {
    std::string open = "<" + tag + ">";
    std::string close = "</" + tag + ">";
    size_t a = s.find(open);
    if (a == std::string::npos) return "";
    a += open.size();
    size_t b = s.find(close, a);
    if (b == std::string::npos) return s.substr(a); // grab to end if no closing tag
    return s.substr(a, b - a);
}
// This function trims leading and trailing whitespace from a string (for the cinical diagnosis formatting)
static std::string trim(const std::string& s) {
            size_t a = s.find_first_not_of(" \t\n\r");
            if (a == std::string::npos) return "";
            size_t b = s.find_last_not_of(" \t\n\r");
            return s.substr(a, b - a + 1);
        }

// ============================================================================
// startWebServer()
// ============================================================================
void startWebServer(const Vitals& current, const std::string& /*unused*/) {

    httplib::Server svr;

    // ---------------------------------------------------------
    // GET /api/vitals
    // ---------------------------------------------------------
    svr.Get("/api/vitals", [&](const httplib::Request&, httplib::Response& res) {

        // 1) pick vitals source (wifi overrides if present)
        Vitals vcopy = (g_liveVitals ? *g_liveVitals : current);
        {
            std::lock_guard<std::mutex> lock(g_vitalsMutex);
            if (g_hasWifiVitals) vcopy = g_latestFromWifi;
        }

        // 2) compute stale FIRST (so we can include it in JSON)
        bool stale = true;
        {
            std::lock_guard<std::mutex> lock(g_vitalsMutex);
            if (g_hasWifiVitals) {
                auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - g_lastRx
                ).count();
                // Right after a POST -> age_ms is tiny -> stale=false
                std::cout << "[vitals] age_ms=" << age_ms
                        << " hasWifi=" << (g_hasWifiVitals ? 1 : 0) << std::endl;
                // After ~3 seconds -> stale=true
                stale = (age_ms > 3000);
            }
        }

        if (!g_liveEnabled) stale = true;

        if (stale) {
            vcopy.HR = -1;
            vcopy.SpO2 = -1;
            vcopy.Temp = -1;
            vcopy.Resp = -1;
            vcopy.BP_sys = -1;
            vcopy.BP_dia = -1;
        }

        // 3) risks (using the chosen vitals)
        int risk_hr   = (vcopy.HR   < 0) ? 0 : calc_HR_risk(vcopy.HR);
        int risk_spo2 = (vcopy.SpO2 < 0) ? 0 : calc_SpO2_risk(vcopy.SpO2);
        int risk_temp = (vcopy.Temp < 0) ? 0 : calc_Temp_risk(vcopy.Temp);
        int risk_resp = (vcopy.Resp < 0) ? 0 : calc_Resp_risk(vcopy.Resp);
        int risk_bp   = (vcopy.BP_sys < 0 || vcopy.BP_dia < 0) ? 0 : calc_BP_risk(vcopy.BP_sys, vcopy.BP_dia);


        // 4) build JSON (make sure commas are correct!)
        std::string json = "{";
        json += "\"hr\":"   + std::to_string(vcopy.HR)     + ",";
        json += "\"spo2\":" + std::to_string(vcopy.SpO2)   + ",";
        json += "\"temp\":" + std::to_string(vcopy.Temp)   + ",";
        json += "\"resp\":" + std::to_string(vcopy.Resp)   + ",";
        json += "\"sys\":"  + std::to_string(vcopy.BP_sys) + ",";
        json += "\"dia\":"  + std::to_string(vcopy.BP_dia) + ",";

        json += "\"risk_hr\":"   + std::to_string(risk_hr)   + ",";
        json += "\"risk_spo2\":" + std::to_string(risk_spo2) + ",";
        json += "\"risk_temp\":" + std::to_string(risk_temp) + ",";
        json += "\"risk_resp\":" + std::to_string(risk_resp) + ",";
        json += "\"risk_bp\":"   + std::to_string(risk_bp)   + ",";

        json += "\"stale\":"     + std::string(stale ? "true" : "false") + ",";
        json += "\"build\":\"vitals_fix_1\"";
        json += "}";



        res.set_content(json, "application/json; charset=utf-8");
    });

//---------------------------------------------------------

    svr.Post("/api/ingest", [&](const httplib::Request& req, httplib::Response& res) {
        const std::string& body = req.body;
        
        if (!g_liveEnabled) {
            // Live is OFF: ignore incoming packets (like if wifi went out or esp32 us unplugged)
            res.status = 204; // No Content
            return;
        }

        // See ESP32 packets printing in terminal
        std::cout << "[ingest] " << body << std::endl;

        // IMPORTANT: tiny JSON parser cannot parse null
        // So for 1/31/26: send -1 for missing values (or omit keys)
        Vitals v;

        //  UI/risk logic plan: -1 = not measured
        v.HR     = (int)jsonGetNumber(body, "HR",   jsonGetNumber(body, "hr",   -1));
        v.SpO2   = (int)jsonGetNumber(body, "SpO2", jsonGetNumber(body, "spo2", -1));
        v.Temp   = (float)jsonGetNumber(body, "Temp", jsonGetNumber(body, "temp", -1));
        v.Resp   = (int)jsonGetNumber(body, "Resp", jsonGetNumber(body, "resp", -1));
        v.BP_sys = (int)jsonGetNumber(body, "BP_sys", jsonGetNumber(body, "sys", -1));
        v.BP_dia = (int)jsonGetNumber(body, "BP_dia", jsonGetNumber(body, "dia", -1));
        {
            std::lock_guard<std::mutex> lock(g_vitalsMutex);
            g_latestFromWifi = v;
            g_hasWifiVitals = true;
            g_lastRx = std::chrono::steady_clock::now();
        }

        res.set_content("OK", "text/plain; charset=utf-8");
    });
    
    // ---------------------------------------------------------
    // GET /api/live  -> return whether live mode is enabled
    // ---------------------------------------------------------
    svr.Get("/api/live", [&](const httplib::Request&, httplib::Response& res) {
        std::string json = std::string("{\"on\":") + (g_liveEnabled ? "true" : "false") + "}";
        res.set_content(json, "application/json; charset=utf-8");
    });

    

    // ---------------------------------------------------------
    // POST /api/live  -> set live mode (expects {"on":true} or {"on":false})
    // ---------------------------------------------------------
    svr.Post("/api/live", [&](const httplib::Request& req, httplib::Response& res) {
        const std::string& body = req.body;

        bool on = (body.find("true") != std::string::npos);
        g_liveEnabled = on;

        res.set_content("OK", "text/plain; charset=utf-8");
    });

    // --------------------------------------------------------
    // POST /api/diagnosis
    // Browser sends JSON with averaged vitals + averaged risks + patient info
    // Server builds prompt via buildClinicalDiagnosisPrompt() and calls sendToLLMChat()
    // ---------------------------------------------------------
    svr.Post("/api/diagnosis", [&](const httplib::Request& req, httplib::Response& res) {

        std::cerr << ">>> /api/diagnosis HIT <<<\n";
        std::cerr << ">>> RAW DEBUG ENABLED <<<\n";

        const std::string& body = req.body;
        //debug: print raw body
        std::cout << "\n[/api/diagnosis] RAW BODY:\n" << body << "\n";

        auto pickStr = [&](std::initializer_list<const char*> keys, const std::string& fb) {
            for (auto k : keys) {
                std::string v = jsonGetString(body, k, "");
                // tiny parser returns fallback if null/unquoted; so empty means "not usable"
                if (!v.empty() && v != "null" && v != "NULL") return v;
            }
            return fb;
        };

        auto pickInt = [&](std::initializer_list<const char*> keys, int fb) {
            for (auto k : keys) {
                int v = jsonGetIntFlexible(body, k, INT32_MIN);
                if (v != INT32_MIN) return v;
            }
            return fb;
        };

        // --- Extract patient meta (supports many possible key names/casing) ---
        std::string name = pickStr({"name","Name","patientName","patient_name","profile_name"}, "Unknown");
        int age          = pickInt({"age","Age","patientAge","patient_age","profile_age"}, 0);
        std::string gender = pickStr({"gender","Gender","sex","Sex","patientGender","patient_gender","profile_gender"}, "Unknown");
        std::string visit = pickStr({"visit","Visit","visitLabel","visit_label"}, "Unknown");

        std::cout << "[diagnosis parsed] name=" << name
                << " age=" << age
                << " gender=" << gender
                << " visit=" << visit << "\n";


        // --- Extract averaged vitals ---
        float hr   = (float)jsonGetNumber(body, "hr", 0);
        float spo2 = (float)jsonGetNumber(body, "spo2", 0);
        float temp = (float)jsonGetNumber(body, "temp", 0);
        float resp = (float)jsonGetNumber(body, "resp", 0);
        float sys  = (float)jsonGetNumber(body, "sys", 0);
        float dia  = (float)jsonGetNumber(body, "dia", 0);

        // --- Extract averaged risks ---
        int risk_hr   = jsonGetInt(body, "risk_hr", 0);
        int risk_spo2 = jsonGetInt(body, "risk_spo2", 0);
        int risk_temp = jsonGetInt(body, "risk_temp", 0);
        int risk_resp = jsonGetInt(body, "risk_resp", 0);
        int risk_bp   = jsonGetInt(body, "risk_bp", 0);

        // =======================
        // Deterministic bypass

        // UPDATED 2/21/26: 
        // This bypass is triggered if ALL risk scores are 0 (instead of just HR) to better align with clinical intuition. 
        // This way we can skip the LLM call for patients who are very low risk across the board, 
        // even if they have a slightly elevated HR that might be a false alarm.
        // =======================
        bool allZero =
            (risk_hr == 0 &&
            risk_spo2 == 0 &&
            risk_temp == 0 &&
            risk_resp == 0 &&
            risk_bp == 0);

        if (allZero) {
            std::ostringstream out;

            out << "Name: " << name << "\n"
                << "Age: " << age << "\n"
                << "Gender: " << gender << "\n"
                << "Date & Time of Visit: " << visit << "\n\n"

                << "------- Patient Vitals: (Averaged data) -------\n\n"
                << "HR: " << hr << " bpm\n"
                << "SpO2: " << spo2 << " %\n"
                << "Temperature: " << temp << " C\n"
                << "Respiratory Rate: " << resp << " rpm\n"
                << "BP: " << sys << "/" << dia << " mmHg\n\n"

                << "Disclaimer: All diagnoses are rendered from an AI and do not constitute professional medical advice.\n\n"

                << "------- Overall Risk Level: -------\n"
                << "Risk 0/3 (Normal). All averaged vitals are within expected clinical ranges.\n\n"

                << "------- Current status of Diagnosis: -------\n"
                << "No abnormal findings. The averaged heart rate, oxygen saturation, temperature, respiration, "
                    "and blood pressure fall within expected ranges. No immediate concerns are indicated by the "
                    "current session averages. Continue routine observation to confirm stability over time.\n\n"

                << "------- Treatment/Goal Plan: -------\n"
                << "Continue routine monitoring during regular sessions. Maintain consistent sensor placement and "
                    "reassess if any readings trend upward or downward across multiple sessions.\n\n";

            res.set_content(out.str(), "text/plain; charset=utf-8");
            return;
        }    
        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // --- Build prompt using your LLM.h API ---
        std::string prompt = buildClinicalDiagnosisPrompt(
            name, age, gender, visit,
            hr, spo2, temp, resp, sys, dia,
            risk_hr, risk_spo2, risk_temp, risk_resp, risk_bp
        );

        // --- Call LLM (your LLM.cpp should return plain text content) ---
        std::string raw = sendToLLMChat(prompt);
        raw = stripLinePrefixes(raw);

        //2/21/26 debug: print raw LLM output after stripping line prefixes
        std::cout << "\n===== RAW (after extractChatContent) =====\n" << raw << "\n";
        std::cerr << "RAW LEN = " << raw.size() << "\n";
        std::cerr << raw << "\n";

        // --- Extract RISK/DX/PLAN sections from the model's output ---
        std::string riskTxt = trim(extractTag(raw, "RISK"));
        std::string dxTxt   = trim(extractTag(raw, "DX"));
        std::string planTxt = trim(extractTag(raw, "PLAN"));

        // fallback if model misbehaves
        if (riskTxt.empty() || dxTxt.empty() || planTxt.empty()) {
            riskTxt = "Unable to format risk summary from LLM output.";
            dxTxt   = "Unable to format diagnosis paragraph from LLM output.";
            planTxt = "Recommend rerunning the session and regenerating the report.";
        }

        // --- Build response content (plain text) ---
        //SPECIFICALLY FORMATTED: THE ONLY THING THE LLM GENERATES IS THE RISK/DX/PLAN SECTIONS.
        std::ostringstream out;
        out << "Name: " << name << "\n"
            << "Age: " << age << "\n"
            << "Gender: " << gender << "\n"
            << "Date & Time of Visit: " << visit << "\n\n"

            << "------- Patient Vitals: (Averaged data) -------\n\n"
            << "HR: " << hr << " bpm\n"
            << "SpO2: " << spo2 << " %\n"
            << "Temperature: " << temp << " C\n"
            << "Respiratory Rate: " << resp << " rpm\n"
            << "BP: " << sys << "/" << dia << " mmHg\n\n"

            << "------- Overall Risk Level: -------\n"
            << riskTxt << "\n\n"

            << "**Disclaimer: All diagnoses are rendered from an AI and do not constitute professional medical advice**\n\n"

            << "------- Current status of Diagnosis: -------\n"
            << dxTxt << "\n\n"

            << "------- Treatment/Goal Plan: -------\n"
            << planTxt << "\n\n";

        res.set_content(out.str(), "text/plain; charset=utf-8");

    });

    // ---------------------------------------------------------
    // GET /
    // UI
    // ---------------------------------------------------------
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
                body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; margin:0; background:#f2f3f5; }
                .app { display:flex; min-height:100vh; }
                .sidebar { width:250px; background:#fff; border-right:1px solid rgba(0,0,0,0.08); padding:18px 14px; box-sizing:border-box; }
                .brand { font-weight:800; font-size:16px; margin-bottom:14px; }
                .nav { display:flex; flex-direction:column; gap:10px; margin-top:10px; }
                .nav button { text-align:left; width:100%; padding:10px 12px; border:1px solid rgba(0,0,0,0.08); border-radius:12px; background:#fff; cursor:pointer; font-size:14px; }
                .nav button.active { background:#f5f7ff; border-color:rgba(0,0,0,0.12); font-weight:700; }
                .content { flex:1; padding:30px 40px; box-sizing:border-box; }
                .topbar { display:flex; align-items:center; gap:10px; margin-bottom:18px; }
                .back-btn { border:1px solid rgba(0,0,0,0.12); background:#fff; border-radius:12px; padding:8px 12px; cursor:pointer; font-size:14px; }
                .page-title { font-weight:800; font-size:20px; margin:0; }
                .muted { opacity:0.7; font-size:13px; }
                .page { display:none; } .page.active { display:block; }
                .list { display:flex; flex-direction:column; gap:10px; margin-top:14px; }
                
                .list-item { background:#fff; border:1px solid rgba(0,0,0,0.08); border-radius:14px; padding:18px 20px; }     

                .container { max-width:900px; margin:0; }
                h2 { text-align:left; margin:0 0 8px 0; font-weight:700; }
                .grid { display:grid; grid-template-columns:1fr 1fr; gap:20px; }
                .card { background:#fff; padding:20px; border-radius:15px; box-shadow:0 4px 12px rgba(0,0,0,0.08); }
                .vital-name { font-size:16px; font-weight:600; }
                .vital-value { font-size:32px; font-weight:700; margin-top:5px; }
                .badge { display:inline-block; padding:4px 10px; color:#fff; border-radius:8px; font-size:13px; margin-top:6px; }
                .summary-card { margin-top:30px; }
                .summary-content { white-space:pre-wrap; margin-top:10px; line-height:1.5; }
                .mini-btn { padding:8px 10px; border-radius:10px; border:1px solid rgba(0,0,0,0.12); background:#fff; cursor:pointer; font-size:13px; }
                .mini-btn:disabled { opacity:0.5; cursor:not-allowed; }
                .field { width:100%; padding:10px; border-radius:10px; border:1px solid rgba(0,0,0,0.12); box-sizing:border-box; }
            </style>
        </head>

        <body>
            <div class="app">
                <aside class="sidebar">
                    <div class="brand">Vitals Monitor</div>
                    <div class="nav">
                        <button id="navPatients" class="active" onclick="go('patients')">Patient Profiles</button>
                        <button id="navLive" onclick="go('live')">Live Vitals</button>
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

                    <section id="pagePatients" class="page active">
                        <div class="card">
                            <div class="vital-name">Patient Profiles (max 2)</div>
                            <div class="muted" style="margin-top:6px;">Edit a profile, then select it before starting a vitals session.</div>

                            <div class="list">
                                <div class="list-item">
                                    <div style="display:flex; justify-content:space-between; align-items:center; gap:12px;">
                                        <div>
                                            <div style="font-size:17px;font-weight:800;" id="pA_name">Not set <span style="font-size:13px;font-weight:400;opacity:0.5;"> (Patient A)</span> <span style="font-size:14px;font-weight:400;color:#777;" id="pA_status"></span></div>
                                            <div style="font-size:14px;margin-top:5px;color:#555;" id="pA_label"></div>
                                        </div>

                                        <div style="display:flex; gap:8px;">
                                            <button class="mini-btn" id="btnEditA" onclick="editPatient(0)">Edit</button>
                                            <button class="mini-btn" id="btnSelectA" onclick="activatePatient(0)">Select</button>
                                            <button class="mini-btn" id="btnClearA" onclick="clearPatient(0)" style="color:#e74c3c;border-color:rgba(231,76,60,0.3);">Clear</button>
                                        </div>
                                    </div>
                                </div>

                                <div class="list-item">
                                    <div style="display:flex; justify-content:space-between; align-items:center; gap:12px;">
                                        <div>
                                            <div style="font-size:17px;font-weight:800;" id="pB_name">Not set <span style="font-size:13px;font-weight:400;opacity:0.5;"> (Patient B)</span> <span style="font-size:14px;font-weight:400;color:#777;" id="pB_status"></span></div>
                                            <div style="font-size:14px;margin-top:5px;color:#555;" id="pB_label"></div>
                                        </div>

                                        <div style="display:flex; gap:8px;">
                                            <button class="mini-btn" id="btnEditB" onclick="editPatient(1)">Edit</button>
                                            <button class="mini-btn" id="btnSelectB" onclick="activatePatient(1)">Select</button>
                                            <button class="mini-btn" id="btnClearB" onclick="clearPatient(1)" style="color:#e74c3c;border-color:rgba(231,76,60,0.3);">Clear</button>
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

                    <section id="pageLive" class="page">
                        <div class="container">
                            <h2>Live Vitals</h2>
                            <div class="muted" style="margin-bottom:12px;">
                                Active patient: <span id="selectedPatientLabel">None</span>
                            </div>

                            <div class="card" style="margin-bottom:14px;">
                                <div class="vital-name">Vitals Session</div>
                                <div class="muted" style="margin-top:6px;">
                                    Logs 1 sample/sec for 3 minutes, then saves a CSV and generates diagnosis.
                                </div>

                                <div style="display:flex; align-items:center; gap:12px; margin-top:12px;">
                                    <button id="startBtn" class="mini-btn" onclick="startVitalsSession()">Start Vitals (3:00)</button>
                                    <button id="stopBtn" class="mini-btn" onclick="stopSession()" disabled>Stop Session</button>

                                    <button id="liveToggleBtn" class="mini-btn" onclick="toggleLive()">Live: ON</button>
                                    <div class="muted">Time left: <span id="timerLabel">3:00</span></div>
                                    <div class="muted" id="sessionStatus"></div>
                                </div>
                            </div>

                            <div class="grid">
        )HTML";

        // VITALS CARDS

        //HR
        html += "<div class='card'><div class='vital-name'>Heart Rate</div>";
        html += "<div id='hrValue' class='vital-value'>" + std::to_string((int)current.HR) + " bpm</div>";
        html += "<div id='hrRisk' class='badge' style='background:" + riskColor(risk["HR"]) + "'>Risk " + std::to_string(risk["HR"]) + "</div></div>";
        //SpO2
        html += "<div class='card'><div class='vital-name'>SpO₂</div>";
        html += "<div id='spo2Value' class='vital-value'>" + std::to_string((int)current.SpO2) + " %</div>";
        html += "<div id='spo2Risk' class='badge' style='background:" + riskColor(risk["SpO2"]) + "'>Risk " + std::to_string(risk["SpO2"]) + "</div></div>";
        //Temp
        html += "<div class='card'><div class='vital-name'>Temperature</div>";
        html += "<div id='tempValue' class='vital-value'>" + std::to_string(current.Temp) + " °C</div>";
        html += "<div id='tempRisk' class='badge' style='background:" + riskColor(risk["Temp"]) + "'>Risk " + std::to_string(risk["Temp"]) + "</div></div>";
        //Resp
        html += "<div class='card'><div class='vital-name'>Respiratory Rate</div>";
        html += "<div id='respValue' class='vital-value'>" + std::to_string((int)current.Resp) + " rpm</div>";
        html += "<div id='respRisk' class='badge' style='background:" + riskColor(risk["Resp"]) + "'>Risk " + std::to_string(risk["Resp"]) + "</div></div>";
        //BP
        html += "<div class='card'><div class='vital-name'>Blood Pressure</div>";
        html += "<div id='bpValue' class='vital-value'>" + std::to_string((int)current.BP_sys) + "/" + std::to_string((int)current.BP_dia) + " mmHg</div>";
        html += "<div id='bpRisk' class='badge' style='background:" + riskColor(risk["BP"]) + "'>Risk " + std::to_string(risk["BP"]) + "</div></div>";

        html += R"HTML(
                            </div>

                            <div class="card summary-card">
                                <div class="vital-name">Clinical Diagnosis (after session)</div>
                                <div id="dxText" class="summary-content">Run a 3-minute session to generate diagnosis.</div>
                                <div id="dxStatus" class="muted" style="margin-top:10px;"></div>
                            </div>
                        </div>
                    </section>

                    <section id="pageDx" class="page">
                    <div class="card">
                        <div class="vital-name">Clinical Diagnoses</div>
                        <div class="muted">Generated after each completed 3-minute session. Download the raw CSV or a PDF summary.</div>
                        <div class="list" id="dxList" style="margin-top:14px;"></div>
                        <div style="margin-top:12px;">
                            <button class="mini-btn danger" onclick="clearAllDx()" style="color:#e74c3c;border-color:rgba(231,76,60,0.3);">Clear all</button>
                        </div>
                    </div>
                </section>

                    <script>
                        const historyStack = [];
                        let currentPage = 'patients';

                        function setActiveNav(page) {
                            document.getElementById('navPatients').classList.toggle('active', page === 'patients');
                            document.getElementById('navLive').classList.toggle('active', page === 'live');
                            document.getElementById('navDx').classList.toggle('active', page === 'diagnosis');
                        }

                        function setPageTitle(page) {
                            const title =
                                (page === 'patients') ? 'Patients' :
                                (page === 'live') ? 'Live Vitals' :
                                (page === 'diagnosis') ? 'Clinical Diagnoses' :
                            document.getElementById('pageTitle').textContent = title;
                        }

                        function showPage(page) {
                            document.getElementById('pagePatients').classList.toggle('active', page === 'patients');
                            document.getElementById('pageLive').classList.toggle('active', page === 'live');
                            document.getElementById('pageDx').classList.toggle('active', page === 'diagnosis');

                            currentPage = page;
                            setActiveNav(page);
                            setPageTitle(page);

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

                            // Grey out "Not set" cards
                            document.getElementById("pA_name").style.opacity = patients[0].name ? "1" : "0.4";
                            document.getElementById("pB_name").style.opacity = patients[1].name ? "1" : "0.4";

                            document.getElementById("pA_name").childNodes[0].textContent = patients[0].name || "Not set ";
                            document.getElementById("pA_label").textContent = patients[0].name ? `${patients[0].gender} • Age ${patients[0].age}` : "";
                            document.getElementById("pB_name").childNodes[0].textContent = patients[1].name || "Not set ";
                            document.getElementById("pB_label").textContent = patients[1].name ? `${patients[1].gender} • Age ${patients[1].age}` : "";

                            document.getElementById("pA_status").textContent =
                                patients[0].lastExport ? `• Last export: ${patients[0].lastExport}` : "";
                            document.getElementById("pB_status").textContent =
                                patients[1].lastExport ? `• Last export: ${patients[1].lastExport}` : "";
                        }
                        
                        function clearPatient(id) {
                            if (sessionRunning) return alert("Stop the session before clearing a profile.");
                            if (!confirm(`Clear Patient ${id === 0 ? "A" : "B"}'s profile?`)) return;
                            patients[id] = { name: "", gender: "", age: "", lastExport: "" };
                            if (activePatientId === id) activePatientId = null;
                            savePatientsToStorage();
                            renderPatientsUI();
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

                        const DX_KEY = "vitals_dx_v1";
                        let dxItems = [];

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

                        function escapeHtml(s) {
                            return String(s)
                                .replaceAll("&","&amp;")
                                .replaceAll("<","&lt;")
                                .replaceAll(">","&gt;")
                                .replaceAll('"',"&quot;")
                                .replaceAll("'","&#39;");
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

                            const riskColor = (r) => {
                                if (r === 0) return "#2ecc71";
                                if (r === 1) return "#f1c40f";
                                if (r === 2) return "#e67e22";
                                return "#e74c3c";
                            };

                            list.innerHTML = sorted.map(d => `
                                <div class="list-item">
                                    <div style="display:flex; justify-content:space-between; align-items:center; gap:12px;">
                                        <div>
                                            <div style="font-size:15px;font-weight:700;">
                                                ${d.patientLabel.split(":")[1]?.trim() || d.patientLabel}
                                                <span class="muted">(${d.patientLabel.split(":")[0]?.trim()})</span>
                                                <span style="display:inline-block;padding:3px 8px;border-radius:6px;font-size:12px;font-weight:600;color:#fff;margin-left:8px;background:${riskColor(d.overallRisk ?? 0)};">Risk ${d.overallRisk ?? 0}</span>
                                            </div>
                                            <div class="muted" style="margin-top:4px;">${new Date(d.createdAtIso).toLocaleString()} &bull; ${d.sampleCount || 0} samples</div>
                                        </div>
                                        <div style="display:flex; gap:8px;">
                                            ${d.csvText ? `<button class="mini-btn" style="color:#27ae60;border-color:rgba(39,174,96,0.3);" onclick="downloadDxCSV('${d.id}')">⬇ Download CSV</button>` : ""}
                                            <button class="mini-btn" style="color:#2980b9;border-color:rgba(41,128,185,0.3);" onclick="downloadDxPDF('${d.id}')">⬇ Download PDF</button>
                                            <button class="mini-btn" style="color:#e74c3c;border-color:rgba(231,76,60,0.3);" onclick="deleteDx('${d.id}')">Delete</button>
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

                        function downloadDxCSV(id) {
                            const d = dxItems.find(x => x.id === id);
                            if (!d || !d.csvText) return alert("No CSV data found for this session.");
                            downloadTextFile(d.csvFilename || "session.csv", d.csvText);
                        }

                        function downloadDxPDF(id) {
                            const d = dxItems.find(x => x.id === id);
                            if (!d) return;

                            const win = window.open("", "_blank");
                            win.document.write(`
                                <html>
                                <head>
                                    <title>${d.patientLabel} - Clinical Diagnosis</title>
                                    <style>
                                        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; padding: 40px; max-width: 800px; margin: 0 auto; }
                                        h1 { font-size: 20px; font-weight: 800; margin-bottom: 4px; }
                                        .muted { color: #888; font-size: 13px; margin-bottom: 24px; }
                                        .content { line-height: 1.8; font-size: 14px; }
                                    </style>
                                </head>
                                <body>
                                    <h1>Clinical Diagnosis Report</h1>
                                    <div class="muted">${d.patientLabel} &bull; ${new Date(d.createdAtIso).toLocaleString()}</div>
                                    <div class="content">${formatDiagnosis(d.text)}</div>
                                    <script>window.onload = () => { window.print(); }<\/script>
                                </body>
                                </html>
                            `);
                            win.document.close();
                        }

                        function clearAllDx() {
                            if (!confirm("Clear all diagnoses?")) return;
                            dxItems = [];
                            saveDx();
                            renderDx();
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

                                const fmt = (x, suffix, digits=null) => {
                                    const n = Number(x);
                                    if (!Number.isFinite(n) || n < 0) return "N/A";
                                    if (digits !== null) return n.toFixed(digits) + suffix;
                                    return Math.round(n) + suffix;
                                };

                                document.getElementById('hrValue').textContent   = fmt(v.hr, " bpm");
                                document.getElementById('spo2Value').textContent = fmt(v.spo2, " %");
                                document.getElementById('tempValue').textContent = fmt(v.temp, " °C", 1);
                                document.getElementById('respValue').textContent = fmt(v.resp, " rpm");
                                document.getElementById('bpValue').textContent   = (Number(v.sys) < 0 || Number(v.dia) < 0) ? "N/A" : (Math.round(v.sys) + "/" + Math.round(v.dia) + " mmHg");

                                const hrR = document.getElementById('hrRisk');
                                const spR = document.getElementById('spo2Risk');
                                const tR  = document.getElementById('tempRisk');
                                const rrR = document.getElementById('respRisk');
                                const bpR = document.getElementById('bpRisk');

                                const stale = v.stale;
                                hrR.textContent  = stale ? "Risk --" : "Risk " + v.risk_hr;   hrR.style.background  = stale ? "#bdc3c7" : riskColorJS(v.risk_hr);
                                spR.textContent  = stale ? "Risk --" : "Risk " + v.risk_spo2; spR.style.background  = stale ? "#bdc3c7" : riskColorJS(v.risk_spo2);
                                tR.textContent   = stale ? "Risk --" : "Risk " + v.risk_temp; tR.style.background   = stale ? "#bdc3c7" : riskColorJS(v.risk_temp);
                                rrR.textContent  = stale ? "Risk --" : "Risk " + v.risk_resp; rrR.style.background  = stale ? "#bdc3c7" : riskColorJS(v.risk_resp);
                                bpR.textContent  = stale ? "Risk --" : "Risk " + v.risk_bp;   bpR.style.background  = stale ? "#bdc3c7" : riskColorJS(v.risk_bp);

                            } catch (e) {}
                        }

                        // -----------------------------
                        // Live ON/OFF toggle logic
                        // -----------------------------

                        let liveOn = true;

                        async function loadLiveState() {
                            try {
                                const r = await fetch("/api/live");
                                const s = await r.json();
                                liveOn = !!s.on;

                                const btn = document.getElementById("liveToggleBtn");
                                if (btn) btn.textContent = liveOn ? "Live: ON" : "Live: OFF";
                            } catch (e) {}
                        }

                        async function toggleLive() {
                            liveOn = !liveOn;

                            const btn = document.getElementById("liveToggleBtn");
                            if (btn) btn.textContent = liveOn ? "Live: ON" : "Live: OFF";

                            try {
                               await fetch("/api/live", {
                                    method: "POST",
                                    headers: { "Content-Type": "application/json" },
                                    body: JSON.stringify({ on: liveOn })
                                }); 
                            } catch (e) {}
                        }

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
                            if (v.stale || Number(v.hr) < 0) return; // don't log invalid data

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

                        function computeAverages(samples) {
                            const n = samples.length || 1;
                            const mean = (arr, key) => arr.reduce((a,x)=>a + Number(x[key]||0), 0) / n;

                            return {
                                hr:   mean(samples, "hr"),
                                spo2: mean(samples, "spo2"),
                                temp: mean(samples, "temp"),
                                resp: mean(samples, "resp"),
                                sys:  mean(samples, "sys"),
                                dia:  mean(samples, "dia"),

                                risk_hr:   Math.round(mean(samples, "risk_hr")),
                                risk_spo2: Math.round(mean(samples, "risk_spo2")),
                                risk_temp: Math.round(mean(samples, "risk_temp")),
                                risk_resp: Math.round(mean(samples, "risk_resp")),
                                risk_bp:   Math.round(mean(samples, "risk_bp"))
                            };
                        }

                        async function generateDiagnosisFromSession(reportId, patient, samples) {
                            const dxTextEl = document.getElementById("dxText");
                            const dxStatusEl = document.getElementById("dxStatus");

                            if (dxStatusEl) dxStatusEl.textContent = "Generating clinical diagnosis…";
                            if (dxTextEl) dxTextEl.textContent = "Generating…";

                            const avg = computeAverages(samples);
                            const visit = new Date().toLocaleString();

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
                                console.log("DX payload:", payload);

                                const r = await fetch("/api/diagnosis", {
                                    method: "POST",
                                    headers: { "Content-Type": "application/json" },
                                    body: JSON.stringify(payload)
                                });

                                const text = await r.text();

                                if (dxTextEl) dxTextEl.innerHTML = formatDiagnosis(text);
                                if (dxStatusEl) dxStatusEl.textContent = "";

                                const id = "dx_" + Math.random().toString(16).slice(2) + "_" + Date.now();
                                const nowIso = new Date().toISOString();
                                const patientLabel = `${activePatientId === 0 ? "Patient A" : "Patient B"}: ${patient.name}`;

                                //-----------------------------
                                // Link the diagnosis to the report by reportId, and save all relevant info in dx
                                //-----------------------------

                                const linkedReport = reports.find(r => r.id === reportId);
                                dxItems.push({ 
                                    id, 
                                    createdAtIso: nowIso, 
                                    patientLabel, 
                                    text, 
                                    reportId,
                                    csvText: linkedReport ? linkedReport.csvText : null,
                                    csvFilename: linkedReport ? linkedReport.filename : null,
                                    sampleCount: linkedReport ? linkedReport.sampleCount : 0,
                                    overallRisk: Math.max(avg.risk_hr, avg.risk_spo2, avg.risk_temp, avg.risk_resp, avg.risk_bp)
                                });

                                saveDx();
                                renderDx();

                            } catch (e) {
                                if (dxStatusEl) dxStatusEl.textContent = "Diagnosis generation failed.";
                                if (dxTextEl) dxTextEl.textContent = "Run another session and try again.";
                            }
                        }
                        // -----------------------------
                        // Making the LLM response look nicer with some basic formatting 
                        // -----------------------------
                        function formatDiagnosis(text) {
                            return text
                                .replace("------- Patient Vitals: (Averaged data) -------", "<b>🩺 Patient Vitals (Averaged)</b>")
                                .replace("------- Overall Risk Level: -------", "<b>⚠️ Overall Risk Level</b>")
                                .replace("------- Current status of Diagnosis: -------", "<b>🔍 Current Status of Diagnosis</b>")
                                .replace("------- Treatment/Goal Plan: -------", "<b>📋 Treatment / Goal Plan</b>")
                                .replace("**Disclaimer: All diagnoses are rendered from an AI and do not constitute professional medical advice**", "<i style='color:#c0392b;font-size:14px;font-weight:bold;'>⚕️ Disclaimer: All diagnoses are rendered from an AI and do not constitute professional medical advice.</i>")                                .replaceAll("\n", "<br>");
                        }

                        // -----------------------------
                        // Vitals session logic: start, stop, save report, generate diagnosis
                        // -----------------------------

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
                            document.getElementById("stopBtn").disabled = false; 

                            document.getElementById("sessionStatus").textContent = "Recording…";

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

                        function stopSession() {
                            if (!sessionRunning) return;
                            clearInterval(countdownTimer);
                            clearInterval(logTimer);
                            countdownTimer = null;
                            logTimer = null;
                            sessionRunning = false;
                            secondsLeft = SESSION_SECONDS;
                            updateTimerUI();
                            lockPatientControls(false);
                            document.getElementById("startBtn").disabled = false;
                            document.getElementById("stopBtn").disabled = true;
                            document.getElementById("sessionStatus").textContent = "Session stopped.";
                            document.getElementById("dxText").textContent = "Session was stopped. Run a new session to generate diagnosis.";
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
                            document.getElementById("stopBtn").disabled = true;

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

                            generateDiagnosisFromSession(reportId, p, sessionSamples)
                                .then(() => { document.getElementById("sessionStatus").textContent = "Complete. Saved to Reports + Diagnosis."; })
                                .catch(() => { document.getElementById("sessionStatus").textContent = "Complete. Saved to Reports. Diagnosis failed."; });
                        }

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
