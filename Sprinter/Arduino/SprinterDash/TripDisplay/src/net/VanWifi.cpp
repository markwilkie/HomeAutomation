#include <Arduino.h>
#include "VanWifi.h"
#include "../../secrets.h"

WebServer server(80);
Logger logger;

const uint32_t connectTimeoutMs = 10000;

void VanWifi::startWifi()
{
  //just return if we're already connected
  if(isConnected())
    return;

  serverOnFlag=false;  //clearly, the server is not yet on

  //Setup wifi
  esp_wifi_start();
  WiFi.disconnect(false);  // Reconnect the network
  WiFi.mode(WIFI_STA);    // Switch WiFi on

  // Add list of wifi networks
  wifiMulti.addAP(VANSSID, PASSWORD);
  wifiMulti.addAP(LFPSSID, PASSWORD);

  //Connects to the wifi with the strongest signal
  if(wifiMulti.run(connectTimeoutMs) == WL_CONNECTED) 
  {
    logger.log(INFO,"Connected to %s, IP: %s",WiFi.SSID().c_str(),WiFi.localIP().toString().c_str());
  }  
  else
  {
    logger.log(ERROR,"Could not connect to Wifi.  Moving on....");
  }

  //send logs
  logger.sendLogs(isConnected());
}

void VanWifi::startServer()
{ 
  // Set server routing and then start
  setupServerRouting();
  server.begin();
  serverOnFlag=true;
  logger.log(INFO,"HTTP server started");  
}

bool VanWifi::isServerOn()
{
  return serverOnFlag;
}

bool VanWifi::isConnected()
{
  return (WiFi.status() == WL_CONNECTED);
}

String VanWifi::getSSID()
{
  return WiFi.SSID();
}

String VanWifi::getIP()
{
  return WiFi.localIP().toString();
}

int VanWifi::getRSSI()
{
  return WiFi.RSSI();
}

void VanWifi::listen()
{
  server.handleClient();
}

void VanWifi::sendResponse(String page)
{
  server.send(200, "text/html", page);
}

bool VanWifi::isPost()
{
  (server.method() == HTTP_POST);
}

// Define routing
void VanWifi::setupServerRouting() {
    server.on("/", HTTP_GET, []() {
        server.send(200,"text/html",
        "/current  --> dump of current data<br>"
        "/trip --> dump of sinceLastStop, currentSegment, and Trip<br>"
        "/tracks --> list GPX track files<br>"
        "/tracks/download?file=FILENAME --> download a GPX file<br>"
        "/tracks/delete?file=FILENAME --> delete a GPX file<br>"
        "/tracks/storage --> show LittleFS usage<br>"
        "/elevation --> altitude auto-calibration status &amp; force recalibrate");
    });

    //GET
    server.on("/current", HTTP_GET, logCurrentData); 
    server.on("/trip", HTTP_GET, logTripData);

#if defined(GPS_ENABLED) && !defined(SIMULATE_GPS)
    //GET - Track files
    server.on("/tracks", HTTP_GET, handleTrackList);
    server.on("/tracks/download", HTTP_GET, handleTrackDownload);
    server.on("/tracks/delete", HTTP_GET, handleTrackDelete);
    server.on("/tracks/storage", HTTP_GET, handleTrackStorage);

    //GET - Elevation calibration status
    server.on("/elevation", HTTP_GET, handleElevationCalib);
#endif
}

void VanWifi::sendErrorResponse(const char*errorString)
{
  String page=errorString;
  server.send(400,"application/text",page);
  
  logger.log(ERROR,"Could not stream error response back to client.  No data found, or incorrect!");
}

// Manage not found URL
void VanWifi::handleNotFound() 
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);

  logger.log(ERROR,"404 - %s",message.c_str());
}
