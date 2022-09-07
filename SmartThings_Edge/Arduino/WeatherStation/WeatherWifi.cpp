#include "WeatherWifi.h"

WebServer server(80);
Logger logger;

void WeatherWifi::startWifi()
{
  serverOnFlag=false;  //clearly, the server is not yet on

  esp_wifi_start();
  WiFi.disconnect(false);  // Reconnect the network
  WiFi.mode(WIFI_STA);    // Switch WiFi on
  logger.log(INFO,"Wifi enabled...");
  
  //Setup wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
 
  // Wait for connection
  int connectCount = 0;
  logger.log(INFO,"Connecting to WIFI ");  
  while (WiFi.status() != WL_CONNECTED) 
  {
    connectCount++;
    delay(500);
    VERBOSEPRINT(".");
    if(connectCount>40)
    {
      //Blink because we couldn't connect to wifi  (5x250)
      blinkLED(5,250,250);

      //Setting flag in flash to indicate that it was a wifiboot
      putBoolPreference("WIFI_BOOT_FLAG",true);

      logger.log(ERROR,"Could not connect to Wifi, restarting board  (obviously this only shows up on a serial connection)");
      ESP.restart();
    }
  }

  logger.log(INFO,"Connected to %s, IP: %S",SSID,WiFi.localIP().toString());

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

void WeatherWifi::startServer()
{ 
  // Set server routing and then start
  setupServerRouting();
  //server.onNotFound(handleNotFound);
  server.begin();
  serverOnFlag=true;
  logger.log(INFO,"HTTP server started");  
}

bool WeatherWifi::isServerOn()
{
  return serverOnFlag;
}

void WeatherWifi::disableWifi()
{ 
  logger.log(INFO,"Disabling Wifi...");  

  //Let's be sure and flush the log cache before shutting down
  logger.sendLogs(isConnected());
  
  WiFi.disconnect(true);  // Disconnect from the network
  WiFi.mode(WIFI_OFF);    // Switch WiFi off
  esp_wifi_stop();
  serverOnFlag=false;
}

bool WeatherWifi::isConnected()
{
  return (WiFi.status() == WL_CONNECTED);
}

int WeatherWifi::getRSSI()
{
  return WiFi.RSSI();
}

void WeatherWifi::listen(long millisToWait)
{
  //take some time to care of webserver stuff  (this will be negotiated in the future)
  logger.log(INFO,"We're now handshaking or in OTA mode - Listening on http and OTA");
  long startMillis=millis();
  while(millis()<(startMillis+millisToWait))
  {
    server.handleClient();
    ArduinoOTA.handle();
  }

  //It's been a bit, so let's send any logs we have
  logger.sendLogs(isConnected());
}

void WeatherWifi::sendResponse(DynamicJsonDocument doc)
{
  String buf;
  serializeJson(doc, buf);
  server.send(200, "application/json", buf);
}

DynamicJsonDocument WeatherWifi::readContent()
{
  String postBody = server.arg("plain");
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, postBody);
  if (error) 
  {
      // if the file didn't open, print an error:
      sendErrorResponse("Error parsing json body! <br>" + (String)error.c_str());
      logger.log(ERROR,"Problem parsing JSON: %S",error);      
  }

  return doc;
}

bool WeatherWifi::isPost()
{
  (server.method() == HTTP_POST);
}

// Define routing
void WeatherWifi::setupServerRouting() {
    server.on("/", HTTP_GET, []() {
        server.send(200,"text/html",
        "Welcome to the REST Web Server");
    });

    //GET
    //server.on("/settings", HTTP_GET, getSettings);    //not currently used 

    //POST
    server.on("/handshake", HTTP_POST, syncWithHub);  //set IP, PORT, and Epoch
}

bool WeatherWifi::sendPostMessage(String url,DynamicJsonDocument doc,int hubPort)
{
  bool success=true;
  WiFiClient client;
  HTTPClient http;
    
  // Your Domain name with URL path or IP address with path
  String ip = String(hubAddress);
  String address="http://"+ip+":"+hubPort+url;
  logger.log(INFO,"Hub Address: %S",address);
  http.begin(client,address.c_str());
  http.addHeader("Content-Type", "application/json");

  // Send HTTP POST request
  String buf;
  serializeJson(doc, buf);
  INFOPRINT("Content: ");
  INFOPRINTLN(buf);
  int httpResponseCode = http.POST(buf);
  
  if (httpResponseCode<0)
  {
    ERRORPRINT("POST http Code: ");
    VERBOSEPRINT(httpResponseCode);
    VERBOSEPRINT(" - ");
    VERBOSEPRINTLN(http.errorToString(httpResponseCode));

    handshakeRequired=true;
    success=false;
  }
  
  // Free resources
  http.end();

  return success;
}

DynamicJsonDocument WeatherWifi::sendGetMessage(String url,int hubPort)
{
  DynamicJsonDocument doc(512);
  WiFiClient client;
  HTTPClient http;
    
  // Your Domain name with URL path or IP address with path
  String ip = String(hubAddress);
  String address="http://"+ip+":"+hubPort+url;
  logger.log(INFO,"Hub Address: %S",address);
  http.begin(client,address.c_str());

  // Send HTTP GET request
  int httpResponseCode = http.GET();

  if (httpResponseCode<0)
  {
    ERRORPRINT("GET http Code: ");
    VERBOSEPRINT(httpResponseCode);
    VERBOSEPRINT(" - ");
    VERBOSEPRINTLN(http.errorToString(httpResponseCode));   

    handshakeRequired=true;
    return doc;
  }

  //get payload
  String payload = http.getString();
    
  // Free resources
  http.end();

  //return payload
  deserializeJson(doc, payload);  
  return doc;
}

void WeatherWifi::sendErrorResponse(String errorString)
{
  DynamicJsonDocument doc(48);
  doc["status"] = "KO";
  doc["message"] = "No data found, or incorrect!";
  
  String buf;
  serializeJson(doc, buf);
  server.send(400,"application/json",buf);
  
  logger.log(ERROR,"Could not stream error response back to client.  No data found, or incorrect!");
}

// Manage not found URL
void WeatherWifi::handleNotFound() 
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

  logger.log(ERROR,"404 - %S",message);
}
