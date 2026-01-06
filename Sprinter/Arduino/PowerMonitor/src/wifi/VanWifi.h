#ifndef wifi_h
#define wifi_h

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>

//Wifi
#define VANSSID "SPRINKLE"
#define LFPSSID "WILKIE-LFP"
#define PASSWORD "4777ne178"

class VanWifi 
{

  public:
    void startWifi();
    void stopWifi();
    String getSSID();
    String getIP();
    int getRSSI();
    bool isConnected();

    DynamicJsonDocument sendGetMessage(const char*url);
    
  private: 

    WiFiMulti wifiMulti;
};

#endif
