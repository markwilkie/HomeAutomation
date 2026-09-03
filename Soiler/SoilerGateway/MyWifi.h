#include "logger.h"
#include "debug.h"

extern MyLogger myLogger;

#ifndef wifi_h
#define wifi_h

#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>
#include <esp_wifi.h>

#define SSID "WILKIE-LFP"
#define PASSWORD "4777ne178"

//declared in main ino program
extern void blinkLED(int,int,int);

class MyWifi
{

  public:
    void startWifi();
    void disableWifi();
    int getRSSI();
    bool isConnected();
    void serviceOTA();
};

#endif
