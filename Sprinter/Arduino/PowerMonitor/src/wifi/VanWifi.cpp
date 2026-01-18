#include "wifi.h"

extern Logger logger;

const uint32_t connectTimeoutMs = 10000;

void VanWifi::startWifi()
{
  //just return if we're already connected
  if(isConnected())
    return;

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
      //logger.log("Progress: %u%%\r", (progress / (total / 100)));
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

void VanWifi::stopWifi()
{
  }
  
  logger.log(INFO, "Stopping WiFi");
  WiFi.disconnect(true);  // disconnect and turn off station mode
  delay(200);  // Allow disconnect to complete
  esp_wifi_stop();
  delay(200);  // Allow mode change to complete  
  // Note: Don't call esp_wifi_stop() - it causes crashes on ESP32-S3
  // WiFi.mode(WIFI_OFF) is sufficient to release GPIO11
  logger.log(INFO, "WiFi stopped");
}

DynamicJsonDocument VanWifi::sendGetMessage(const char*url)
{
  DynamicJsonDocument doc(512);
  HTTPClient http;

  // Check if URL is HTTPS
  bool isHttps = (strncmp(url, "https://", 8) == 0);
  
  if (isHttps) {
    WiFiClientSecure *clientSecure = new WiFiClientSecure();
    clientSecure->setInsecure();  // Skip certificate validation for simplicity
    http.begin(*clientSecure, url);
  } else {
    WiFiClient *client = new WiFiClient();
    http.begin(*client, url);
  }
  
  http.setTimeout(45000);  // Set timeout to 45 seconds

  // Send HTTP GET request
  int httpResponseCode = http.GET();

  if (httpResponseCode<0)
  {
    ERRORPRINT("GET http Code: ");
    VERBOSEPRINT(httpResponseCode);
    VERBOSEPRINT(" - ");
    VERBOSEPRINTLN(http.errorToString(httpResponseCode));   

    return doc;
  }

  //get payload
  String payload = http.getString();
    
  // Free resources
  http.end();

  Serial.print("GET Response payload: ");
  Serial.println(payload);

  //return payload
  deserializeJson(doc, payload);  
  return doc;
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
