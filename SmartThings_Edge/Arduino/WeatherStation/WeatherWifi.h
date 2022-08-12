#ifndef weatherwifi_h
#define weatherwifi_h

#include "WeatherStation.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>

//Wifi
#define SSID "WILKIE-LFP"
#define PASSWORD "4777ne178"

//declared in main ino program
extern void refreshWindRain();
extern void refreshBME();
extern void refreshADC();
extern void refreshPMS();
extern void getSettings();
extern void debugData();
extern void setEpoch();
extern void syncWithHub();

class WeatherWifi 
{

  public:
    void initWifi();
    void startServer();
    bool isPost();
    void listen(long);
    DynamicJsonDocument readContent();
    void sendResponse(DynamicJsonDocument);

    void sendPostMessage(DynamicJsonDocument);
    
  private: 
    void setupServerRouting();
    void handleNotFound();
    void sendErrorResponse(String);
};

#endif
