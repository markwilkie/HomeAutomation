#include "logger.h"
#include "debug.h"

extern Logger logger;

#ifndef wifi_h
#define wifi_h

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>


//Wifi
#define SSID "WILKIE-LFP"
#define PASSWORD "4777ne178"

//declared in main ino program
extern void logCurrentData();
extern void logTripData();

class Wifi 
{

  public:
    void startWifi();
    void startServer();
    int getRSSI();
    bool isPost();
    bool isConnected();
    bool isServerOn();
    void listen();
    void sendResponse(String);
    
  private: 
    void setupServerRouting();
    void handleNotFound();
    void sendErrorResponse(const char*);

    bool serverOnFlag;
};

#endif
