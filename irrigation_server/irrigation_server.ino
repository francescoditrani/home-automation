#include <WiFi.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
#include <uri/UriRegex.h>
#include "wificonf.h"

#define IVY_ANALOG_PIN 36
#define IVY_SENSOR_POWER 23
#define BASIL_ANALOG_PIN 32
#define BASIL_SENSOR_POWER 33
#define RELAY_1 16
#define RELAY_2 17

#define TRIG_PIN 14 
#define ECHO_PIN 19

const uint16_t ivy_air_val = 4095;
const uint16_t ivy_water_val = 900;

const uint16_t basil_air_val = 4095;
const uint16_t basil_water_val = 700;

const int max_irrigation_sec = 5;
const int min_water_level = 20;

const char* ssid = SSID; 
const char* password = WIFI_PASSWORD; 
const String hostname = "esp32-irrigation";

float duration_us, distance_cm;

WebServer server(80);

String header;


void connect_wifi() {
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(hostname.c_str());
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
}


float waterLevel() {
  Serial.println("Getting water level..");

  // generate 10-microsecond pulse to TRIG pin
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // measure duration of pulse from ECHO pin
  duration_us = pulseIn(ECHO_PIN, HIGH);

  // calculate the distance
  distance_cm = 0.017 * duration_us;
  Serial.print("Distance: ");
  Serial.println(distance_cm);

  float water_level = map(distance_cm, 14.7, 3.0, 0, 100);
  Serial.print("Water level: ");
  Serial.println(water_level);
  return water_level;
}

float getWaterLevel() {
  float water_level = waterLevel();
  char buf[10];
  sprintf (buf, "%d", (int)water_level);  
  server.send(200, "text/plain", buf);
}

uint16_t readMoisture(uint8_t powerPin, uint8_t analogPin, uint16_t airValue, uint16_t waterValue) {
  uint16_t moistVal;
  digitalWrite(powerPin, HIGH);
  delay(10);
  moistVal = analogRead(analogPin);
  Serial.println(moistVal);
  digitalWrite(powerPin, LOW);
  moistVal = map(moistVal, waterValue, airValue, 100, 0);
  Serial.println(moistVal);
  return moistVal;
}

String toString(uint16_t integer) {
  char buf[10];
  sprintf (buf, "%d", integer);
  return buf;
}

void triggerRelay(int relay, int seconds) {
  if (seconds > 0 && seconds <=max_irrigation_sec) {
    
    Serial.println( "relay on..");
    digitalWrite(relay, LOW);
    delay(seconds * 1000);
    Serial.println("relay off..");
    digitalWrite(relay, HIGH);
    
    server.send(200, "text/plain", "done");
  } else {
    server.send(400, "text/plain", "too long irrigation");
  }
}

void setup() {
  Serial.begin(115200);

  //configure soil moisture pins
  pinMode(IVY_ANALOG_PIN,INPUT);
  adcAttachPin(IVY_ANALOG_PIN);
  pinMode(BASIL_ANALOG_PIN,INPUT);
  adcAttachPin(BASIL_ANALOG_PIN);
  
  pinMode(IVY_SENSOR_POWER, OUTPUT);
  digitalWrite(IVY_SENSOR_POWER, LOW);
  pinMode(BASIL_SENSOR_POWER, OUTPUT);
  digitalWrite(BASIL_SENSOR_POWER, LOW);

  // configure distance sensor pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  //configure relay pins
  pinMode(RELAY_1, OUTPUT);
  digitalWrite(RELAY_1, HIGH);
  pinMode(RELAY_2, OUTPUT);
  digitalWrite(RELAY_2, HIGH);

  connect_wifi();

  server.on("/water/level", getWaterLevel);

  server.on(UriBraces("/moisture/{}"), [](){
    String plant = server.pathArg(0);
    if (plant == "ivy") {
      uint16_t moisturePerc = readMoisture(IVY_SENSOR_POWER, IVY_ANALOG_PIN, ivy_air_val, ivy_water_val);
      server.send(200, "text/plain", toString(moisturePerc));
    } else if (plant == "basil") {
      uint16_t moisturePerc = readMoisture(BASIL_SENSOR_POWER, BASIL_ANALOG_PIN, basil_air_val, basil_water_val);
      server.send(200, "text/plain", toString(moisturePerc));
    } else {
      server.send(400, "text/plain", "'plant' query param can only be 'ivy' or 'basil'");
    }
  });


  server.on(UriBraces("/irrigate/plant/{}/seconds/{}"), [](){
    //check first for the distance, bad request in case too low level
    int water_level = waterLevel();
    if (water_level > min_water_level) {
      String plant = server.pathArg(0);
      int seconds = atoi((server.pathArg(1)).c_str());
      Serial.print("requested seconds: ");
      Serial.println(seconds);
      if (plant == "ivy") {
        triggerRelay(RELAY_1, seconds);
      } else if (plant == "basil") {
        triggerRelay(RELAY_2, seconds);
      } else {
        server.send(400, "text/plain", "'plant' query param can only be 'ivy' or 'basil'");
      }
    } else {
      server.send(400, "text/plain", "not enough water!");
    }
  });
  
  server.begin();
}



void loop() {     
  server.handleClient();     
}
