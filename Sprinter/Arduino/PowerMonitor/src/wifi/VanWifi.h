#ifndef wifi_h
#define wifi_h

/*
 * VanWifi - WiFi connection manager with multi-AP support
 * 
 * Manages WiFi connectivity for the van power monitor system.
 * Supports multiple access points (van hotspot + home network).
 * 
 * FEATURES:
 * - Multi-AP support via WiFiMulti (connects to strongest signal)
 * - Auto-reconnect disabled (managed manually to avoid conflicts)
 * - HTTP GET requests with JSON parsing for time API
 * - Graceful stop/start for ADC reading conflicts
 * 
 * IMPORTANT: WiFi must be stopped before reading certain ADC pins
 * (GPIO11, GPIO13) due to ESP32-S3 hardware conflicts. Use stopWifi()
 * before tank sensor reads, then startWifi() after.
 * 
 * USAGE:
 *   VanWifi wifi;
 *   wifi.startWifi();           // Connect to strongest AP
 *   if(wifi.isConnected()) {
 *       DynamicJsonDocument doc = wifi.sendGetMessage(url);
 *   }
 *   wifi.stopWifi();            // Stop for ADC reads
 */

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>

// ============================================================================
// WIFI CREDENTIALS - Customize for your networks
// ============================================================================
#define VANSSID "SPRINKLE"       // Van hotspot SSID
#define LFPSSID "WILKIE-LFP"     // Home network SSID
#define PASSWORD "4777ne178"     // Shared password for both networks

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
    bool apAdded = false;         // Track if APs have been added
};

#endif
