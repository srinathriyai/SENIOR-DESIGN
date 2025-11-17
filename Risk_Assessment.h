#ifndef RISK_LEVELS_H
#define RISK_LEVELS_H

int calc_HR_risk(float hr);
int calc_SpO2_risk(float sp);
int calc_Temp_risk(float t);
int calc_Resp_risk(float r);
int calc_BP_risk(float sys, float dia);

#endif
