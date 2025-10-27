/* 
  =======================================================
  Vitals Monitoring System – LLM Integration (Pseudocode)
  -------------------------------------------------------
  Goal: 
  1. ESP32 reads and sends sensor data to Laptop
  2. Laptop computes risk levels (0–3) for each vital
  3. Laptop auto-generates an LLM prompt (text)
  4. LLM returns clinical-style interpretation for each vital
  =======================================================
*/

//headers
#include <iostream>
#include <string>
#include <map>
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
*/

// Repeat for other vital functions later
int calc_HR_risk(float hr) {
    if (hr >= 60 && hr <= 100) {
        return 0; // NORMAL
    } else if ((hr > 100 && hr <= 110) || (hr >= 50 && hr < 60)) {
        return 1; // MILD RISK
    } else if ((hr > 110 && hr <= 130) || (hr >= 40 && hr < 50)) {
        return 2; // MODERATE RISK
    } else {
        return 3; // HIGH/CRITICAL RISK
    }
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
        "Given the following vitals and risk levels, "
        "generate a short clinical-style explanation for each and a one-sentence summary.\n\n"
        "Vitals:\n";

  /*
      (int)v.HR   : gets the actual heart rate number (ex/ 110)
      risk["HR"]  : gets the corresponding risk value (ex/ 2)
      to_string() : turns both numbers into text so they can be combined
  */
    prompt += "• HR: " + to_string((int)v.HR) + " bpm (risk " + to_string(risk["HR"]) + ")\n";
    prompt += "• SpO₂: " + to_string((int)v.SpO2) + " % (risk " + to_string(risk["SpO2"]) + ")\n";
    prompt += "• Temp: " + to_string(v.Temp) + " °C (risk " + to_string(risk["Temp"]) + ")\n";
    prompt += "• Resp: " + to_string((int)v.Resp) + " rpm (risk " + to_string(risk["Resp"]) + ")\n";
    prompt += "• BP: " + to_string((int)v.BP_sys) + "/" + to_string((int)v.BP_dia) + " mmHg (risk " + to_string(risk["BP"]) + ")\n";

    return prompt;
}

//IDK HOW TO DO THIS STEP I GOTTA FIGURE IT OUT :(
/*
--------------------------------Step 4: Send prompt to LLM (placeholder)--------------------------------
(In the real system, make HTTP request to API or local model) 
*/

//CHAT GAVE THIS TO ME I GOTTA RE-WRITE IT 
string sendToLLM(string prompt) {
    // Display the outgoing prompt so we can debug or verify its formatting
    cout << "Sending prompt to LLM:\n" << prompt << endl;
    
    /*
    -------------------------------------------------------
    TODO: Replace the placeholder below with actual HTTP request code
     Examples:
      - Use libcurl (C++) for a POST request to an API endpoint
      - Or use HTTPClient if running on ESP32 with Wi-Fi
    
     The server should respond with text (the model’s generated output)
     which will then be stored or displayed by the laptop program.
    --------------------------------------------------------
    */

    // Placeholder response (simulates what the LLM might return)
    string llmResponse = 
        "HR: Elevated — may indicate fever.\n"
        "SpO₂: Slightly low — monitor oxygen levels.\n"
        "Temp: Mild fever detected.\n"
        "Resp: Slightly high breathing rate.\n"
        "BP: Normal range.\n"
        "Summary: Moderate physiological stress, possibly mild infection.";
    
  return llmResponse;
}

// --------------------------------Step 5: Main loop simulation------------------------------------

int main() {
    // Example data from ESP32
    Vitals current = {110, 93, 38.2, 22, 125, 85};

    // Compute risk scores
    map<string, int> risk;
    risk["HR"] = calc_HR_risk(current.HR);
    risk["SpO2"] = calc_SpO2_risk(current.SpO2);
    //!!!!!repeat for other vitals!!!!!

    // Build LLM prompt
    string prompt = generatePrompt(current, risk);

    // Send prompt and get interpretation
    string report = sendToLLM(prompt);

    // Display result (would be saved or logged in real version)
    cout << "\n--- LLM Response ---\n" << report << endl;

    return 0;
}



