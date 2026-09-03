#include "MyWifi.h"

MyLogger myLogger;

void MyWifi::startWifi()
{
  esp_wifi_start();
  WiFi.disconnect(false);  // Reconnect the network
  WiFi.mode(WIFI_STA);    // Switch WiFi on

  //Setup wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);

  // Wait for connection
  int connectCount = 0;
  myLogger.log(VERBOSE,"Connecting to WIFI...");
  while (WiFi.status() != WL_CONNECTED)
  {
    connectCount++;
    delay(1000);
    if(connectCount>10)
    {
      //Blink because we couldn't connect to wifi  (5x250)
      blinkLED(5,250,250);

      myLogger.log(ERROR,"Could not connect to Wifi, restarting board  (obviously this only shows up on a serial connection)");
      ESP.restart();
    }
  }

  myLogger.log(INFO,"Connected to %s, IP: %s",SSID,WiFi.localIP().toString().c_str());

  //Init OTA
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      myLogger.log(WARNING,"Starting OTA updating ");
    })
    .onEnd([]() {
      myLogger.log(INFO,"\nEnd OTA");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      myLogger.log(ERROR,"OTA Problem: %s",error);
      if (error == OTA_AUTH_ERROR) { myLogger.log(ERROR,"Auth Failed"); }
      else if (error == OTA_BEGIN_ERROR){ myLogger.log(ERROR,"Begin Failed"); }
      else if (error == OTA_CONNECT_ERROR){ myLogger.log(ERROR,"Connect Failed"); }
      else if (error == OTA_RECEIVE_ERROR) { myLogger.log(ERROR,"Receive Failed"); }
       else if (error == OTA_END_ERROR) { myLogger.log(ERROR,"End Failed"); }
    });

  ArduinoOTA.begin();

  //send logs
  myLogger.sendLogs(isConnected());
}

void MyWifi::disableWifi()
{
  myLogger.log(VERBOSE,"Disabling Wifi...");

  //Let's be sure and flush the log cache before shutting down
  myLogger.sendLogs(isConnected());

  WiFi.disconnect(true);  // Disconnect from the network
  WiFi.mode(WIFI_OFF);    // Switch WiFi off
  esp_wifi_stop();
}

bool MyWifi::isConnected()
{
  return (WiFi.status() == WL_CONNECTED);
}

int MyWifi::getRSSI()
{
  return WiFi.RSSI();
}

void MyWifi::serviceOTA()
{
  ArduinoOTA.handle();
}
