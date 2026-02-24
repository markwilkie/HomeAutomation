#ifndef wifi_h
#define wifi_h

#include "../Globals.h"
#include "../logging/logger.h"
#include "../logging/Debug.h"

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>

extern Logger logger;

//Wifi credentials defined in secrets.h

//declared in main ino program
extern void logCurrentData();
extern void logTripData();
extern void handleTrackList();
extern void handleTrackDownload();
extern void handleTrackDelete();
extern void handleTrackStorage();
extern void handleElevationCalib();

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
