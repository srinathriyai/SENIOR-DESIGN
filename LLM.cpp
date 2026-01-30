/* 
  =======================================================
  Vitals Monitoring System – LLM Integration (Pseudocode)
  -------------------------------------------------------
  WHAT THE LLM DOES
  - Simulate or receive vitals from ESP32
  - Compute risk scores based on threshold table
  - Generate structured LLM prompt
  - Send prompt to local llama.cpp server (localhost:8080)
  - Display and log interpretation
  =======================================================
*/

//headers
#include <iostream>
#include <string>
#include <map>
#include <sstream>
#include <fstream>

#include <curl/curl.h> // libcurl lib

#include "Risk_Assessment.h"
#include "WebServer.h"

using namespace std;

/*------------------------------------Step 4: libcurl helper for writing responses------------------------------------*/
/*
This function collects the LLM’s reply as it arrives in small pieces
    -> libcurl calls this every time it gets a chunk of data from the server

Parameter meanings:
- 'contents' → the new piece of text we just got
- 'size' and 'nmemb' → how big that piece is (size * nmemb)
- 'output' → where we’re saving the reply text

We add each new piece to 'output' until the full response is complete. Return the number of bytes we used so libcurl knows it worked.
*/

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// --------------------------------Step 4.5: JSON formatting??------------------------------------
// --------------------------------------------------------------
// jsonEscape() --- CHATGPT RECCOMENDS THIS? IDK I GOTTA FIGURE THIS OUT TOO
// --------------------------------------------------------------
// Purpose:
//   Makes your text safe to send inside a JSON message.
//   (Used when sending your prompt to the LLM server.)
//
// Why you need this:
//   JSON uses special characters like quotes " and newlines \n.
//   If your prompt contains these characters, it can break the
//   JSON format and cause errors when the LLM tries to read it.
//
// Example problem:
//     "Patient says: "I feel dizzy""
//   This breaks JSON because of the extra quotes.
//
//   jsonEscape() fixes it so it becomes:
//     "Patient says: \"I feel dizzy\""
//
// In short: this function "cleans" your text so it’s valid JSON.
// --------------------------------------------------------------

std::string jsonEscape(const std::string& s) {
    std::string out;                 // new string that will store the safe version
    out.reserve(s.size() + 16);      // make space in memory (a small optimization)

    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                out += c;
                break;
        }
    }
  
    return out;  // return the cleaned, JSON-safe string
}

// --------------------------------Step 3: Build a structured text prompt for the LLM--------------------------------
/*
Purpose:
   Convert numerical sensor data + computed risk levels into a formatted English sentence block. 
   The LLM can only read TEXT, not variables. This function prepares the text that will be sent to the model.
*/

string generatePrompt(Vitals v, map<string, int> risk) {
/* EXPLAINING ALL THE PARAMETERS:
    - Vitals ('v') is a struct: a small container that holds all your raw sensor readings together as one “package"
          - giving the function (generatePrompt) access to all the vital readings at once
    - The "map" (IMPORTED IN HEADER) stores key–value pairs, where:
          - The key (string) is the vital’s name: "HR", "SpO2", "Temp", etc
          - The value (int) is that vital’s risk level (0–3) from the math model

  HOW THE PROMPT IS GENERATED:
    - Inside your generatePrompt() function:
          - combine readings from Vitals ('v') and risk levels ('map<string,int> risk') into a readable sentence
          - ex/ the result for heart rate prompt: "• HR: 110 bpm (risk 2)"

*/
  
  // The message has to have clear task instructions for the LLM
  // The instruction tells the model what to do with the data we fed into it
    std::ostringstream oss;

    oss << "You are a clinical assistant. Analyze the following vital signs and their risk levels.\n";
    oss << "Provide a short, structured explanation for each vital and a one-sentence summary.\n\n";

    oss << "Vitals:\n";
    oss << "• HR: "   << v.HR      << " bpm (risk " << risk.at("HR")   << ")\n";
    oss << "• SpO₂: " << v.SpO2    << " % (risk "   << risk.at("SpO2") << ")\n";
    oss << "• Temp: " << v.Temp    << " °C (risk "  << risk.at("Temp") << ")\n";
    oss << "• Resp: " << v.Resp    << " rpm (risk " << risk.at("Resp") << ")\n";
    oss << "• BP: "   << v.BP_sys  << "/" << v.BP_dia << " mmHg (risk " << risk.at("BP") << ")\n\n";

    oss << "Respond in a brief clinical summary style.\n";

    return oss.str();
}


/*------------------------------------Step 5: Send prompt to local llama.cpp server------------------------------------*/
/*
This function sends our prompt text to the local LLM server and returns the model’s reply

*/

std::string sendToLLM(const std::string& prompt) {
    std::string response;

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL\n";
        return "ERROR: CURL init failed";
    }

    std::string url = "http://localhost:8080/completion";

    std::string jsonData = "{ \"prompt\": \"" + jsonEscape(prompt) + "\", \"n_predict\": 200 }";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, jsonData.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    // Optional: set header for JSON
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "LLM request failed: " << curl_easy_strerror(res) << "\n";
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // Optional: log to file
    std::ofstream log("llm_reports.txt", std::ios::app);
    if (log.is_open()) {
        log << "===== Prompt =====\n"   << prompt    << "\n\n";
        log << "===== Response =====\n" << response  << "\n\n";
    }

    return response;
}

// Extract the "content" field from llama.cpp JSON response
std::string extractContent(const std::string& json) {
    std::string key = "\"content\":\"";
    size_t start = json.find(key);
    if (start == std::string::npos) return json; // fallback

    start += key.length();
    size_t end = json.find("\"", start);
    if (end == std::string::npos) return json;

    std::string content = json.substr(start, end - start);

    // Optional: replace escaped characters
    while (content.find("\\n") != std::string::npos)
        content.replace(content.find("\\n"), 2, "\n");

    while (content.find("\\\"") != std::string::npos)
        content.replace(content.find("\\\""), 2, "\"");

    return content;
}


// --------------------------------Step 6: Main loop simulation------------------------------------

int llm_test_main() {
    return 0;
}

