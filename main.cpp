#include <Arduino.h>
#include <tempSensor.h>
#include <hrSensor.h>
#include <respSensor.h>
//other includes? interrupt etc as needed

int testHrVal = 0;
int testtempVal = 0;
int testRespVal = 0;
int testBpVal = 0;
//set values within individual respective functions to mimic getting from sensors
//confirm good/bad values respond correctly (no server/risk assessment right now, connect later)

//global variables
  //timer variable
  //


//functions
  //confirm sensor function (yellow on functional block diagram)
  //confirm server connection (^^^)

  //take sample function (call to headers) and send to server 

  //function for combining LLM risk assessment

  //function(s) for summary wording from LLM risk assessment



int main(void){
//variables

//check BT/wifi module connection 

//confirm sensor function
//confirm server function

//timer loop 
  //take sample function + send data

  //receive back data values for ranking (for input to wording summary)
    //function for wording summary (takes ranking values as input)
    



}

