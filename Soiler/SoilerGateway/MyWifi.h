#include "logger.h"
#include "debug.h"

extern MyLogger myLogger;

#ifndef wifi_h
#define wifi_h

#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>

#define SSID "WILKIE-LFP"
#define PASSWORD "4777ne178"


//declared in main ino program
extern void getSettings();
extern void debugData();
extern void setEpoch();
extern void syncWithHub();

extern void blinkLED(int,int,int);

extern unsigned long epoch;  //Epoch from hub
extern char hubAddress[30];

class MyWifi 
{

  public:
    void startWifi();
    void startServer();
    void disableWifi();
    int getRSSI();
    bool isPost();
    bool isConnected();
    bool isServerOn();
    void listen(long);
    DynamicJsonDocument readContent();
    void sendResponse(DynamicJsonDocument);
    DynamicJsonDocument readJsonFile(char*);

    bool sendPostMessage(const char*,DynamicJsonDocument,int);
    bool sendPutMessage(const char*fullurl,const char*token,const char*buf);
    DynamicJsonDocument sendGetMessage(const char*,int);
    
  private: 
    void setupServerRouting();
    void handleNotFound();
    void sendErrorResponse(const char*);

    bool serverOnFlag;
};

#endif
