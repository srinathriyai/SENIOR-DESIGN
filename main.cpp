#include <Arduino.h>
#include <tempSensor.h>
#include <hrSensor.h>
#include <respSensor.h>
//other includes? interrupt etc as needed


//global variables
  //timer variable
  //


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
    avgTemp = 1;          //rank 1 meaning bad
  }

  return tempRank;
}

int main(void){
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
  sampleHeartRate(sampledHrAvg); 
  sampleRespiratory(sampledRespAvg);
  sampleTemperature(sampledTempAvg);


  return 0;
}
