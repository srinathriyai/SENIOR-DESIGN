// FILE NAME: LLM.h
#pragma once
#include <string>

// Build the clinical diagnosis prompt in your exact template (filled with values).
std::string buildClinicalDiagnosisPrompt(
    const std::string& name,
    int age,
    const std::string& gender,
    const std::string& visit,
    float hr, float spo2, float temp, float resp, float sys, float dia,
    int risk_hr, int risk_spo2, int risk_temp, int risk_resp, int risk_bp
);

// Sends prompt to llama.cpp OpenAI-compatible endpoint (/v1/chat/completions).
// Returns plain-text content (already extracted).
std::string sendToLLMChat(const std::string& prompt);
