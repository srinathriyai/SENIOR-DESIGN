#ifndef RISK_ASSESSMENT_H
#define RISK_ASSESSMENT_H

#include <string>
#include <map>

// ---------------------------
// DEFINE VITALS STRUCT HERE
// ---------------------------
struct Vitals {
    float HR;
    float SpO2;
    float Temp;
    float Resp;
    float BP_sys;
    float BP_dia;
};

// ---------------------------
// RISK FUNCTIONS ONLY
// ---------------------------
int calc_HR_risk(float hr);
int calc_SpO2_risk(float sp);
int calc_Temp_risk(float t);
int calc_Resp_risk(float r);
int calc_BP_risk(float sys, float dia);

#endif
