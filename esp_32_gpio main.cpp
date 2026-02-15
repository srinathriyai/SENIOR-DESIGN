/*
  ------ESP32 GPIO Test------
  ESP32 connects to WiFi -> opens a tiny website -> 
  browser talks directly to ESP32 -> ESP32 toggles pins
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

//DEFINED FOR THE FREENOVE ESP32 DEV BOARD, CHANGE IF USING A DIFFERENT BOARD
//wroom dev-kit: GPIO2 & GPIO4
//arduino nano: 
#define LED1 2 
#define LED2 4 

// REPLACE WITH CURRENT WiFi SSID AND PASSWORD
const char* ssid = "HriyaiIP15";
const char* password = "Km^uzw@3032";

WebServer server(80); // port 80, the default for HTTP

bool led1State = false;
bool led2State = false;

void handleRoot() { //The browser sends GET /led1 and GET /led2 when the user clicks the links on the webpage
  /*
  String page = "<h1>ESP32 GPIO Test</h1>";
  page += "<p><a href=\"/led1\">Toggle GPIO2</a></p>";
  page += "<p><a href=\"/led2\">Toggle GPIO4</a></p>";
  server.send(200, "text/html", page);
  */
  String page = "<h1>ESP32 GPIO Test</h1>";
  page += "<p>GPIO2 state: " + String(led1State ? "ON" : "OFF") + "</p>";
  page += "<p>GPIO4 state: " + String(led2State ? "ON" : "OFF") + "</p>";
  page += "<p><a href=\"/led1\">Toggle GPIO2</a></p>";
  page += "<p><a href=\"/led2\">Toggle GPIO4</a></p>";
  server.send(200, "text/html", page);
}




/*  
  FOR BOTH toggleLED1() AND toggleLED2():

  1. Flips the state (true → false → true)
  2. Sets GPIO HIGH or LOW
  3. Prints confirmation to Serial
  4. Redirects browser back to home page

*/ 
void toggleLED1() {
  led1State = !led1State;
  digitalWrite(LED1, led1State);
  Serial.println("GPIO2 toggled");
  server.sendHeader("Location", "/");
  server.send(303);
}

void toggleLED2() {
  led2State = !led2State;
  digitalWrite(LED2, led2State);
  Serial.println("GPIO4 toggled");
  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  // Starts serial communication
  Serial.begin(115200);
  delay(200);

  // Sets GPIO pins as outputs
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);

  // Connects to WiFi
  
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  unsigned long t0 = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("WiFi status = ");
  Serial.println(WiFi.status());

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("FAILED to connect.");
    // optional: restart to retry cleanly
    // ESP.restart();
    return;
  }
  /*
  //checking for the available wifi

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(500);

  Serial.println("Scanning...");
  int n = WiFi.scanNetworks();
  Serial.printf("Found %d networks\n", n);

  for (int i = 0; i < n; i++) {
    Serial.printf("%2d) %s  RSSI=%d  %s\n",
      i + 1,
      WiFi.SSID(i).c_str(),
      WiFi.RSSI(i),
      (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "OPEN" : "SECURED"
    );
  }
  */
  // Prints IP address
  Serial.println("\nConnected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // Tells the web server what functions handle which routes
  server.on("/", handleRoot);
  server.on("/led1", toggleLED1);
  server.on("/led2", toggleLED2);

  // Starts the web server
  server.begin();
}
/*
This ALWAYS checks:

Did a browser send a request?
If yes → it runs the correct function.

  **without this line, the web server wouldn’t respond**
*/
void loop() {
  server.handleClient();
}
