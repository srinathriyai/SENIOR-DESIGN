//the Webserver needs to call to my LLM functs.

#pragma once
#include <string>
#include <map>
#include "WebServer.h"   // for Vitals

std::string generatePrompt(Vitals v, std::map<std::string,int> risk);
std::string sendToLLM(const std::string& prompt);
std::string extractContent(const std::string& json);
