// FILE NAME: LLM.cpp
#include "LLM.h"
#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <string>
// MODEL USED:
// https://huggingface.co/meta-llama/Llama-3.2-3B-Instruct

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
// UPDATED 2/21/26
// It finds "role":"assistant" first, then extracts the content after that, 
// instead of grabbing the first "content" in the whole JSON


static std::string extractChatContent(const std::string& json) {
     // Prefer the assistant role content (more reliable than first "content")
    size_t rolePos = json.find("\"role\":\"assistant\"");
    if (rolePos == std::string::npos) {
        // fallback: last "content" in the response
        rolePos = json.rfind("\"content\":\"");
        if (rolePos == std::string::npos) return json;
    }
    
    const std::string key = "\"content\":\"";
    size_t start = json.find(key, rolePos);
    if (start == std::string::npos) return json; // fallback (shows error body)
    start += key.size();

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


    /*
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
    
    */
    
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
//UPDATED 2/21/26

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
      << "You are generating text for a senior design demo system. Not medical advice.\n"
      << "Use ONLY the numeric vitals and risk levels provided. Do NOT invent symptoms, history, diagnoses, meds, or labs.\n"
      << "If a vital value is -1 it was not measured this session. Do not comment on it.\n"
      << "Do NOT output Name/Age/Gender/Visit. Do NOT repeat the vitals list.\n\n"

      << "Vitals (averaged): "
      << "HR=" << hr << " bpm, "
      << "SpO2=" << spo2 << " %, "
      << "Temp=" << temp << " C, "
      << "Resp=" << resp << " rpm, "
      << "BP=" << sys << "/" << dia << " mmHg.\n"

      << "Risk levels (0-3): "
      << "HR=" << risk_hr << ", "
      << "SpO2=" << risk_spo2 << ", "
      << "Temp=" << risk_temp << ", "
      << "Resp=" << risk_resp << ", "
      << "BP=" << risk_bp << ".\n\n"

      << "Return ONLY the text inside the following template. Keep the tags.\n"
      << "No markdown. No bullets. No extra lines before <RISK>.\n\n"

      << "<RISK>\n"
      << "(Write 1-2 sentences summarizing overall risk level and meaning. If all risks are 0, state no abnormal findings.)\n"
      << "</RISK>\n\n"

      << "<DX>\n"
      << "(Write 4-5 complete sentences, paragraph. Mention any abnormal vitals explicitly. If all risks are 0, state all vitals normal.)\n"
      << "</DX>\n\n"

      << "<PLAN>\n"
      << "(Write 2-4 complete sentences, paragraph. Monitoring and follow-up only. No medications.)\n"
      << "</PLAN>\n";

    return oss.str();
}

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
            "\"max_tokens\":500"
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
    return content;
    //return extractBetweenTags(content, "<OUTPUT>\n", "\n</OUTPUT>");
}
