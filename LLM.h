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
#include <fstream>
#include <curl/curl.h> // libcurl library
using namespace std;

/*
--------------------------------Step 1: Simulated (FOR NOW) input data from ESP32--------------------------------
(In the real system, this comes from BLE or Wi-Fi)
*/ 

struct Vitals {
    float HR;
    float SpO2;
    float Temp;
    float Resp;
    float BP_sys;
    float BP_dia;
};

/*
--------------------------------Step 2: Compute risk levels using simple threshold logic--------------------------------
(Each function returns an int from 0–3)
Returns a value: 0 = Normal, 1 = Mild, 2 = Moderate, 3 = Severe/Critical
*/

int calc_HR_risk(float hr) {
    if (hr >= 60 && hr <= 100)
        return 0;
    else if ((hr > 100 && hr <= 110) || (hr >= 50 && hr < 60))
        return 1;
    else if ((hr > 110 && hr <= 130) || (hr >= 40 && hr < 50))
        return 2;
    else
        return 3;
}

int calc_SpO2_risk(float sp) {
    if (sp >= 95)
        return 0;
    else if (sp >= 90)
        return 1;
    else if (sp >= 85)
        return 2;
    else
        return 3;
}

int calc_Temp_risk(float t) {
    if (t >= 36.1 && t <= 37.5)
        return 0;
    else if ((t > 37.5 && t <= 38.0) || (t >= 35.5 && t < 36.1))
        return 1;
    else if ((t > 38.0 && t <= 39.0) || (t >= 35.0 && t < 35.5))
        return 2;
    else
        return 3;
}

int calc_Resp_risk(float r) {
    if (r >= 12 && r <= 20)
        return 0;
    else if ((r > 20 && r <= 24) || (r >= 10 && r < 12))
        return 1;
    else if ((r > 24 && r <= 30) || (r >= 8 && r < 10))
        return 2;
    else
        return 3;
}

int calc_BP_risk(float sys, float dia) {
    if (sys < 120 && dia < 80)
        return 0;
    else if ((sys < 130 && dia < 80) || (sys >= 80 && dia <= 85))
        return 1;
    else if ((sys < 140 && dia < 90))
        return 2;
    else
        return 3;
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
  string prompt =
        "You are a clinical assistant. Analyze the following vital signs and their risk levels.\n"
        "Provide a short, structured explanation for each vital and a one-sentence summary.\n\n"
        "Vitals:\n";

  /*
      (int)v.HR   : gets the actual heart rate number (ex/ 110)
      risk["HR"]  : gets the corresponding risk value (ex/ 2)
      to_string() : turns both numbers into text so they can be combined
  */
    prompt += "• HR: " + to_string((int)v.HR) + " bpm (risk " + to_string(risk["HR"]) + ")\n";
    prompt += "• SpO₂: " + to_string((int)v.SpO2) + "% (risk " + to_string(risk["SpO2"]) + ")\n";
    prompt += "• Temp: " + to_string(v.Temp) + " °C (risk " + to_string(risk["Temp"]) + ")\n";
    prompt += "• Resp: " + to_string((int)v.Resp) + " rpm (risk " + to_string(risk["Resp"]) + ")\n";
    prompt += "• BP: " + to_string((int)v.BP_sys) + "/" + to_string((int)v.BP_dia)
            + " mmHg (risk " + to_string(risk["BP"]) + ")\n\n"
            "Respond in a brief clinical summary style.\n";

    return prompt;
}

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

size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* output) {
    // Calculate how many bytes we got in this chunk
    size_t totalSize = size * nmemb;    
    // Append the raw bytes to our output buffer (std::string)
    // We cast 'contents' (void*) to (char*) to append the bytes
    output->append((char*)contents, totalSize); 
    
  // Tell libcurl we successfully handled 'totalSize' bytes
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

    // Go through each character in the input string
    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;  // turn " into \"
            case '\\': out += "\\\\"; break;  // turn \ into \\
            case '\b': out += "\\b";  break;  // escape backspace
            case '\f': out += "\\f";  break;  // escape formfeed
            case '\n': out += "\\n";  break;  // escape newline
            case '\r': out += "\\r";  break;  // escape carriage return
            case '\t': out += "\\t";  break;  // escape tab
            default:
                // If it's a control character (non-printable ASCII)
                if (static_cast<unsigned char>(c) <= 0x1F) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", c); // encode as \u00XX
                    out += buf;
                } else {
                    out += c;  // normal character, keep it
                }
        }
    }

    return out;  // return the cleaned, JSON-safe string
}

/*------------------------------------Step 5: Send prompt to local llama.cpp server------------------------------------*/
/*
This function sends our prompt text to the local LLM server and returns the model’s reply

Here’s what happens:
1. Set up a network connection (libcurl)
2. Tell it the server address → "http://localhost:8080/completion"
3. Pack our prompt into JSON format to send
4. Send the JSON to the LLM and wait for a response
5. Collect the reply text using our WriteCallback function
6. Clean up and return the full reply as a string

Note:
- llama.cpp must be running locally on port 8080
- Compile with '-lcurl' so the program can use libcurl
*/

// This function sends your text prompt to the local LLM server
// (llama.cpp running at http://localhost:8080/completion)
// and gives back the model's reply as a string.

std::string sendToLLM(const std::string& prompt) {
    CURL* curl;             // curl = tool that sends/receives data through the web
    CURLcode res;           // stores success or error code from curl
    std::string response;   // where we’ll store the model’s text reply

    // STEP 1: Start curl. This sets up everything we need to make a web request
    curl = curl_easy_init();
    if (curl) {
        // STEP 2: Tell curl where to send the request
                // llama.cpp listens on this address and port
        std::string url = "http://localhost:8080/completion";

        // STEP 3: Make sure our text is safe to send as JSON
                // (It fixes quotes, newlines, etc. so it doesn’t break the format)
        std::string safePrompt = jsonEscape(prompt);

        // STEP 4: Build the JSON message we’ll send to the LLM
                // "prompt" = what we want the model to read
                // "n_predict" = how many words/tokens we want it to generate
        std::string jsonData =
            std::string("{\"prompt\":\"") + safePrompt + "\",\"n_predict\":200}";

        // STEP 5: Create headers that say “this is JSON data.”
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        // STEP 6: Set up all the curl options

        // The website (or local server) we’re sending data to
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        // Add the “Content-Type: application/json” header
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // The actual data we want to send in the request body
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());

        // Tell curl to use our WriteCallback function to collect the reply text
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);

        // Tell curl where to store the incoming reply (in the string “response”)
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        // Limit how long to wait for the model to reply (in seconds)
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

        // STEP 7: Send the request and wait for the model’s response
        // This line actually sends our data to llama.cpp and blocks until we get an answer
        res = curl_easy_perform(curl);

        // STEP 8: Check if the request worked
        if (res != CURLE_OK) {
            std::cerr << "LLM request failed: "
                      << curl_easy_strerror(res) << std::endl;
        }

        // STEP 9: Free up the memory used for headers and the curl handle
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    } else {
        // If starting curl didn’t work, show an error
        std::cerr << "Failed to start libcurl." << std::endl;
    }

    // STEP 10: Give back the model’s reply text to the rest of the program
    return response;
}

// --------------------------------Step 6: Main loop simulation------------------------------------

int main() {
    // Example vitals 
    Vitals current = {110, 92, 38.2, 22, 128, 86};

    // Compute risks
    map<string, int> risk;
    risk["HR"] = calc_HR_risk(current.HR);
    risk["SpO2"] = calc_SpO2_risk(current.SpO2);
    risk["Temp"] = calc_Temp_risk(current.Temp);
    risk["Resp"] = calc_Resp_risk(current.Resp);
    risk["BP"] = calc_BP_risk(current.BP_sys, current.BP_dia);

    // Build and display prompt
    string prompt = generatePrompt(current, risk);
    cout << "===== Prompt Sent to LLM =====\n" << prompt << endl;

    // Send to llama.cpp and get results
    string llmResponse = sendToLLM(prompt);
    cout << "\n===== LLM Response =====\n" << llmResponse << endl;

    // Save results
    ofstream log("llm_reports.txt", ios::app);
    log << "Prompt:\n" << prompt << "\n---Response---\n" << llmResponse << "\n\n";
    log.close();

    return 0;
}
