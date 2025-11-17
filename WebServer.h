#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <string>
#include "httplib.h"
#include "Risk_Assessment.h"

void startWebServer(const Vitals& current, const std::string& llmResponse);

#endif
