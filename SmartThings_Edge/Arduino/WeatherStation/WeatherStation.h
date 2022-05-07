#include "Arduino.h"
#include <WiFiClient.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

//globals

long epoch;  //Epoch from hub
