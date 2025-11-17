#include "Risk_Assessment.h"

// Heart Rate
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

// SpO2
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

// Temperature
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

// Respiratory Rate
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

// Blood Pressure
int calc_BP_risk(float sys, float dia) {
    if (sys < 120 && dia < 80)
        return 0;
    else if ((sys < 130 && dia < 80) || (sys >= 80 && dia <= 85))
        return 1;
    else if (sys < 140 && dia < 90)
        return 2;
    else
        return 3;
}
