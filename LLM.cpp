// FILE NAME: LLM.cpp
#include "LLM.h"
#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <string>

// ============================================================================
// LLM.cpp (Local LLM Client + Prompt Builder)
//
// Purpose:
//   - Builds a structured “clinical diagnosis” prompt from averaged session vitals
//   - Sends the prompt to a local llama.cpp server (OpenAI-compatible endpoint)
//   - Returns ONLY the model’s generated content text for display/storage
//
// Main functions:
//   buildClinicalDiagnosisPrompt() -> fills the exact template shown in the UI
//   sendToLLMChat()                -> HTTP POST to /v1/chat/completions via libcurl
//
// Helpers:
//   WriteCallback()      -> collects the HTTP response body from libcurl (chunked)
//   jsonEscape()         -> escapes prompt text so it can be embedded safely in JSON
//   extractChatContent() -> MVP string-based parsing to pull the assistant "content"
//
// Key behavior / assumptions:
//   - Uses a local server at http://localhost:8081 (update if your port changes)
//   - Sends system + user messages in OpenAI chat JSON format
//   - Does NOT use a full JSON library; parsing is minimal and assumes predictable
//     LLM response structure ("content":"...") from llama-server
//   - On failure, returns readable error strings (curl failure or non-200 HTTP)
// ============================================================================

// ---------------- libcurl write callback ----------------
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    std::string* out = static_cast<std::string*>(userp);
    out->append(static_cast<char*>(contents), total);
    return total;
}

// ------------- minimal JSON escaping for prompt -------------
static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 32);
    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

// -------- Extract choices[0].message.content from OpenAI JSON --------
// This is intentionally “string find” MVP parsing (no JSON library).
static std::string extractChatContent(const std::string& json) {
    // Look for: "content":"...."
    const std::string key = "\"content\":\"";
    size_t start = json.find(key);
    if (start == std::string::npos) return json; // fallback (shows error body)
    start += key.size();

    // Find the ending quote that is not escaped.
    std::string out;
    out.reserve(512);

    bool escape = false;
    for (size_t i = start; i < json.size(); i++) {
        char c = json[i];
        if (escape) {
            if (c == 'n') out.push_back('\n');
            else if (c == 'r') out.push_back('\r');
            else if (c == 't') out.push_back('\t');
            else out.push_back(c);
            escape = false;
            continue;
        }
        if (c == '\\') { escape = true; continue; }
        if (c == '"') break;
        out.push_back(c);
    }

    return out.empty() ? json : out;
}
// ---------------- Extract content between tags (e.g., <OUTPUT>) ----------------
// This can be used to pull out the specific section of the model's 
// output if you include tags in your prompt and want to ensure only 
// that is being returned
static std::string extractBetweenTags(const std::string& s,
                                     const std::string& openTag,
                                     const std::string& closeTag) {
    size_t a = s.find(openTag);
    if (a == std::string::npos) return s;
    a += openTag.size();
    size_t b = s.find(closeTag, a);
    if (b == std::string::npos) return s.substr(a);
    return s.substr(a, b - a);
}

// ---------------- Build prompt with template FILLED ----------------
//UPDATED 2/19/26

std::string buildClinicalDiagnosisPrompt(
    const std::string& name,
    int age,
    const std::string& gender,
    const std::string& visit,
    float hr, float spo2, float temp, float resp, float sys, float dia,
    int risk_hr, int risk_spo2, int risk_temp, int risk_resp, int risk_bp
) {
    std::ostringstream oss;

    oss
    << "ENGLISH ONLY.\n"
    << "Use ONLY the values below. Do NOT invent conditions or extra data.\n"
    << "If ALL risks are 0, write: \"No abnormal findings.\" and give routine monitoring bullets.\n\n"

    << "Patient: " << name << ", " << age << ", " << gender << "\n"
    << "Visit: " << visit << "\n\n"

    << "Averages: HR " << hr << " bpm; SpO2 " << spo2 << " %; Temp " << temp
    << " C; Resp " << resp << " rpm; BP " << sys << "/" << dia << " mmHg.\n"
    << "Risks (0-3): HR " << risk_hr << ", SpO2 " << risk_spo2
    << ", Temp " << risk_temp << ", Resp " << risk_resp << ", BP " << risk_bp << ".\n\n"

    << "Output EXACTLY in this format:\n"
    << "Assessment: <1-2 sentences>\n"
    << "Plan:\n"
    << "- <bullet>\n"
    << "- <bullet>\n";
   // << "**Disclaimer: All diagnoses are rendered from an AI and do not constitute professional medical advice**\n";

    return oss.str();
}

/*
    std::string buildClinicalDiagnosisPrompt(
    const std::string& name,
    int age,
    const std::string& gender,
    const std::string& visit,
    float hr, float spo2, float temp, float resp, float sys, float dia,
    int risk_hr, int risk_spo2, int risk_temp, int risk_resp, int risk_bp
) {
    std::ostringstream oss;

    oss
    << "You are a clinical summary generator for a senior engineering project.\n"
    << "This is NOT a real medical diagnosis.\n\n"

    << "Use ONLY the numeric values and risk levels provided below.\n"
    << "Do NOT invent measurements, symptoms, history, or lab results.\n"
    << "Do NOT invent or modify patient identity fields.\n"
    << "Do NOT output placeholders, refusal text, or extra disclaimers.\n\n"

    << "INTERPRETATION RULES:\n"
    << "- Risk level 0 = normal / within expected range\n"
    << "- Risk level 1 = mildly abnormal\n"
    << "- Risk level 2 = moderately abnormal\n"
    << "- Risk level 3 = severely abnormal\n"
    << "- If ALL risk levels are 0, state that no abnormal findings are detected.\n"
    << "- Base your wording strictly on risk levels and values.\n\n"

    // IMPORTANT: Do NOT let the model print identity (it will hallucinate on small models).
    << "OUTPUT RULES (FOLLOW EXACTLY):\n"
    << "- Output ONLY these two sections, in this order:\n"
    << "  1) Current Status of Diagnosis:\n"
    << "  2) Treatment / Goal Plan:\n"
    << "- Do NOT output Name, Age, Gender, or Date/Time.\n"
    << "- Do NOT reprint the vitals list.\n"
    << "- Do NOT add any other headings or sections.\n\n"

    // Identity provided ONLY as context (not allowed in output)
    << "Patient identity (context only; do not repeat in output):\n"
    << "- Name: " << name << "\n"
    << "- Age: " << age << "\n"
    << "- Gender: " << gender << "\n"
    << "- Date & Time of Visit: " << visit << "\n\n"

    << "Patient Vitals (Averaged data):\n"
    << "- HR: " << hr << " bpm\n"
    << "- SpO2: " << spo2 << " %\n"
    << "- Temperature: " << temp << " C\n"
    << "- Respiratory Rate: " << resp << " rpm\n"
    << "- Blood Pressure: " << sys << "/" << dia << " mmHg\n\n"

    << "Risk Levels (Averaged):\n"
    << "- HR Risk: " << risk_hr << "\n"
    << "- SpO2 Risk: " << risk_spo2 << "\n"
    << "- Temperature Risk: " << risk_temp << "\n"
    << "- Respiratory Risk: " << risk_resp << "\n"
    << "- Blood Pressure Risk: " << risk_bp << "\n\n"

    // Keep exactly ONE disclaimer line inside the model output (as you wanted),
    // but do not allow it to add more.
    << "Include this disclaimer EXACTLY ONCE at the end of your output:\n"
    << "\"**Disclaimer: All diagnoses are rendered from an AI and do not constitute professional medical advice**\"\n\n"

    << "Current Status of Diagnosis:\n"
    << "- Write exactly 2 to 4 complete sentences.\n"
    << "- Describe overall physiological status using the provided risks.\n"
    << "- Mention any mildly, moderately, or severely abnormal vitals explicitly.\n"
    << "- If all risks are 0, explicitly state that all vitals are within normal limits.\n\n"

    << "Treatment / Goal Plan:\n"
    << "- Write exactly 2 to 4 bullet points.\n"
    << "- Bullets should describe monitoring, follow-up, or reassessment only.\n"
    << "- Do NOT recommend medications, procedures, or emergency actions.\n"
    << "- If all risks are 0, recommend routine monitoring only.\n";

    return oss.str();
}
*/


// ---------------- Send to llama-server ----------------
std::string sendToLLMChat(const std::string& prompt) {
    
    std::cout << "\n===== PROMPT SENT TO LLM =====\n";
    std::cout << prompt << "\n";
    std::cout << "==============================\n\n";
    
    // IMPORTANT: set these to match your llama-server
    const std::string host = "http://localhost:8081";
    const std::string url  = host + "/v1/chat/completions";

    std::string response;



    CURL* curl = curl_easy_init();
    if (!curl) return "ERROR: CURL init failed";

    // OpenAI-compatible chat JSON
    std::string json =
        "{"
            "\"messages\":["
                //"{\"role\":\"system\",\"content\":\"You are a clinical assistant.\"},"
                //"{\"role\":\"system\",\"content\":\"You are a clinical summary generator. Follow the user instructions exactly. Do not invent patient identity.\"},"
                "{\"role\":\"system\",\"content\":\"You must respond in English only. Follow the user format exactly. Do not invent diseases. If all risks are 0, say 'No abnormal findings.'\"},"
                "{\"role\":\"user\",\"content\":\"" + jsonEscape(prompt) + "\"}"
            "],"
            "\"temperature\":0.1,"
            "\"max_tokens\":350,"
            "\"stop\":[\"</OUTPUT>\"]"
        "}";
    
    std::cout << "\n===== JSON SENT TO LLM SERVER =====\n";
    std::cout << json << "\n";
    std::cout << "===================================\n\n";

    

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)json.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode rc = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        return std::string("ERROR: curl_easy_perform failed: ") + curl_easy_strerror(rc);
    }
    if (httpCode != 200) {
        // This will show you the real server error (404, etc.)
        return "ERROR: LLM HTTP " + std::to_string(httpCode) + "\n\n" + response;
    }

    std::cout << "\n\n===== RAW LLM SERVER RESPONSE =====\n";
    std::cout << response << "\n";
    std::cout << "====================================\n\n";


    //return extractChatContent(response);

    //UI gets only the clean two sections + disclaimer
    std::string content = extractChatContent(response);
    return extractBetweenTags(content, "<OUTPUT>\n", "\n</OUTPUT>");
}
