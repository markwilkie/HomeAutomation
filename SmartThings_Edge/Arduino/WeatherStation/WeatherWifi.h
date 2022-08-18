#ifndef weatherwifi_h
#define weatherwifi_h

#include "WeatherStation.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>

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

extern void blinkLED(int,int,int);

class WeatherWifi 
{

  public:
    void startWifi();
    void startServer();
    void disableWifi();
    bool isPost();
    bool isConnected();
    void listen(long);
    DynamicJsonDocument readContent();
    void sendResponse(DynamicJsonDocument);

    void sendPostMessage(String,DynamicJsonDocument);
    DynamicJsonDocument sendGetMessage(String);
    
  private: 
    void setupServerRouting();
    void handleNotFound();
    void sendErrorResponse(String);
};

#endif
