//#include <Arduino.h>
#include <stdio.h> //here for testing, CHANGED platform.ini to framework = espidf  instead of arduino
#include <string.h>
//IF ARDUINO PLUGGED IN CHANGE ^^^^^

//#include <tempSensor.h>
//#include <hrSensor.h>
//#include <respSensor.h>
//#include <bp.h>
//other includes? interrupt etc as needed


//global variables and definitions
  //timer variable
  //define ranking variables?, for sample rank output of functions instead of defining in functions

//functions
  //confirm sensor function (yellow on functional block diagram)
  //confirm server connection (^^^)

  //take sample function (call to headers) and send to server 

  //function for combining LLM risk assessment

  //function(s) for summary wording from LLM risk assessment

unsigned int sampleHeartRate(int avgHr){    //Converting heart rate average to rank severity score and outputting
  int hrRank;                               //For storing rank

  if(avgHr >= 1){       //1 for testing, signifying good
    hrRank = 0;         //rank 0 meaning safe range
  }
  else if(avgHr == 0){  //0 for testing, signifying bad
    hrRank = 1;         //rank 1 meaning bad
  }

  return hrRank;  //output
}

unsigned int sampleRespiratory(int avgResp){  //Converting respiratory average to rank severity score and outputting
  int respRank;                               //For storing rank

  if(avgResp >= 1){     //1 for testing, signifying good
    respRank = 0;       //rank 0 meaning safe range
  }
  else if(avgResp == 0){
    respRank = 1;
  }

  return respRank;  //output
}

unsigned int sampleTemperature(int avgTemp){  //Converting temperature average to rank severity score and outputting
  int tempRank;

  if(avgTemp >= 1){     //1 for testing, signifying good
    tempRank = 0;       //rank 0 meaning safe range
  }
  else if(avgTemp == 0){
    tempRank = 1;          //rank 1 meaning bad
  }

  return tempRank;
}

void summaryResults(int hr, int resp, int temp){   //Take sample ranks and print summary of results (make more wording later on)
//Testing block just for now
  char sumHr[10];
  char sumResp[10];
  char sumTemp[10];
//if else statements for safe and unsafe
  if(hr >= 1){
    strcpy(sumHr, "safe");    //dummy string
  }
  else if(hr == 0){
    strcpy(sumHr, "unsafe");
  }

  if(resp >= 1){
    strcpy(sumResp, "safe");
  }
  else if(resp == 0){
    strcpy(sumResp, "unsafe");
  }

  if(temp >= 1){
    strcpy(sumTemp, "safe");
  }
  else if(temp == 0){
    strcpy(sumTemp, "unsafe");
  }

  //output print, can format better later
  printf("Patient's Heart Rate is within %s range.\n", sumHr); printf("Patient's Respiratory Rate is within %s range.\n", sumResp); printf("Patient's Temperature is within %s range.\n", sumTemp);
  //this is arduino format below
  /*
  Serial.print("Patient's Heart Rate is within ");
  Serial.print(sumHr);
  Serial.println(" range.");

  Serial.print("Patient's Respiratory Rate is within ");
  Serial.print(sumResp);
  Serial.println(" range.");

  Serial.print("Patient's Temperature is within ");
  Serial.print(sumTemp);
  Serial.println(" range.");
  */
}

int main(){            //need to be changed to void setup(){} for arduino
//Serial.begin(115200);   //esp32, uncomment if plugged to arduino
//variables

//check BT/wifi module connection 

//confirm sensor function
//confirm server function

//timer loop 
  //take sample function + send data

  //receive back data values for ranking (for input to wording summary)
    //function for wording summary (takes ranking values as input)
    

//testing code for output, assuming values are taken as averages from individual sensor block codes

  //dummy variables for results from sampling ( replace with calling to sensor header files )
  int sampledHrAvg = 1;   
  int sampledRespAvg = 0; 
  int sampledTempAvg = 1;

  //take samples
  int hrRank = sampleHeartRate(sampledHrAvg); 
  int respRank = sampleRespiratory(sampledRespAvg);
  int tempRank = sampleTemperature(sampledTempAvg);

  //summary results
  summaryResults(hrRank, respRank, tempRank);

  return 0;
}


void loop() { //requried for arduino include
  //can be left empty for now
}

