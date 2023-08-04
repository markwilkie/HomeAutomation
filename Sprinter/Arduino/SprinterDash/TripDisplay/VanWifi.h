#ifndef wifi_h
#define wifi_h

#include "logger.h"
#include "debug.h"

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>

extern Logger logger;

//Wifi
#define VANSSID "SPRINKLE"
#define LFPSSID "WILKIE-LFP"
#define PASSWORD "4777ne178"

//declared in main ino program
extern void logCurrentData();
extern void logTripData();

class VanWifi 
{

  public:
    void startWifi();
    void startServer();
    String getSSID();
    String getIP();
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

    WiFiMulti wifiMulti;
    bool serverOnFlag;
};

#endif
