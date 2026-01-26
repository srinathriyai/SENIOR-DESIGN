#ifndef WEBSERVER_H
#define WEBSERVER_H

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00   // Windows 10
#endif
#endif

#include <string>
#include "external/httplib.h"   // <--- use ONE header path
#include "Risk_Assessment.h"

// Launches the web dashboard at http://localhost:8000
//void startWebServer(const Vitals& current, const std::string& llmResponse);

// NEW: give the web server access to a live-updating Vitals struct
// You will call this once in pc_main.cpp: setLiveVitalsSource(&liveVitals);
void setLiveVitalsSource(const Vitals* live);

// Launches the web dashboard at http://localhost:8000
void startWebServer(const Vitals& current, const std::string& llmResponse);


#endif
