#include "VanWifi.h"

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
  //WiFi.begin(SSID, PASSWORD);

  //Connects to the wifi with the strongest signal
  if(wifiMulti.run(connectTimeoutMs) == WL_CONNECTED) 
  {
    logger.log(INFO,"Connected to %s, IP: %s",WiFi.SSID().c_str(),WiFi.localIP().toString().c_str());
  }  
  else
  {
    logger.log(ERROR,"Could not connect to Wifi.  Moving on....");
  }

  //Init OTA
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      logger.log(WARNING,"Starting OTA updating ");
    })
    .onEnd([]() {
      logger.log(INFO,"\nEnd OTA");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      logger.log(ERROR,"OTA Problem: %s",error);
      if (error == OTA_AUTH_ERROR) { logger.log(ERROR,"Auth Failed"); }
      else if (error == OTA_BEGIN_ERROR){ logger.log(ERROR,"Begin Failed"); }
      else if (error == OTA_CONNECT_ERROR){ logger.log(ERROR,"Connect Failed"); }
      else if (error == OTA_RECEIVE_ERROR) { logger.log(ERROR,"Receive Failed"); }
       else if (error == OTA_END_ERROR) { logger.log(ERROR,"End Failed"); }
    });

  ArduinoOTA.begin();

  //send logs
  logger.sendLogs(isConnected());
}

void VanWifi::startServer()
{ 
  // Set server routing and then start
  setupServerRouting();
  //server.onNotFound(handleNotFound);
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
  ArduinoOTA.handle();
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
        "/current  --> dump of current data<br>/trip --> dump of sinceLastStop, currentSegment, and Trip");
    });

    //GET
    server.on("/current", HTTP_GET, logCurrentData); 
    server.on("/trip", HTTP_GET, logTripData);

    //POST
    //server.on("/handshake", HTTP_POST, syncWithHub);  //set IP, PORT, and Epoch
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
