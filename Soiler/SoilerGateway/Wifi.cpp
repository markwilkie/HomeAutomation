#include "Wifi.h"

WebServer server(80);
Logger logger;

void Wifi::startWifi()
{
  serverOnFlag=false;  //clearly, the server is not yet on

  esp_wifi_start();
  WiFi.disconnect(false);  // Reconnect the network
  WiFi.mode(WIFI_STA);    // Switch WiFi on
  
  //Setup wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
 
  // Wait for connection
  int connectCount = 0;
  logger.log(VERBOSE,"Connecting to WIFI...");  
  while (WiFi.status() != WL_CONNECTED) 
  {
    connectCount++;
    delay(1000);
    if(connectCount>10)
    {
      //Blink because we couldn't connect to wifi  (5x250)
      blinkLED(5,250,250);

      logger.log(ERROR,"Could not connect to Wifi, restarting board  (obviously this only shows up on a serial connection)");
      ESP.restart();
    }
  }

  logger.log(INFO,"Connected to %s, IP: %s",SSID,WiFi.localIP().toString().c_str());

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

void Wifi::startServer()
{ 
  // Set server routing and then start
  setupServerRouting();
  //server.onNotFound(handleNotFound);
  server.begin();
  serverOnFlag=true;
  logger.log(INFO,"HTTP server started");  
}

bool Wifi::isServerOn()
{
  return serverOnFlag;
}

void Wifi::disableWifi()
{ 
  logger.log(VERBOSE,"Disabling Wifi...");  

  //Let's be sure and flush the log cache before shutting down
  logger.sendLogs(isConnected());
  
  WiFi.disconnect(true);  // Disconnect from the network
  WiFi.mode(WIFI_OFF);    // Switch WiFi off
  esp_wifi_stop();
  serverOnFlag=false;
}

bool Wifi::isConnected()
{
  return (WiFi.status() == WL_CONNECTED);
}

int Wifi::getRSSI()
{
  return WiFi.RSSI();
}

void Wifi::listen(long secondsToWait)
{
  //take some time to care of webserver stuff  (this will be negotiated in the future)
  if(secondsToWait==0)
  {
    server.handleClient();
    ArduinoOTA.handle();
    return;
  }    

  unsigned long startMillis=millis();
  while(millis()<(startMillis+(secondsToWait*1000L)))
  {
    server.handleClient();
    ArduinoOTA.handle();
  }

  //It's been a bit, so let's send any logs we have
  logger.sendLogs(isConnected());
}

void Wifi::sendResponse(DynamicJsonDocument doc)
{
  String buf;
  serializeJson(doc, buf);
  server.send(200, "application/json", buf);
}

DynamicJsonDocument Wifi::readContent()
{
  String postBody = server.arg("plain");
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, postBody);
  if (error) 
  {
      // if the file didn't open, print an error:
      sendErrorResponse(error.c_str());
      logger.log(ERROR,"Problem parsing JSON: %s",error.c_str());      
  }

  return doc;
}

bool Wifi::isPost()
{
  return (server.method() == HTTP_POST);
}

// Define routing
void Wifi::setupServerRouting() {
    server.on("/", HTTP_GET, []() {
        server.send(200,"text/html",
        "Welcome to the REST Web Server");
    });

    //GET
    //server.on("/settings", HTTP_GET, getSettings);    //not currently used 

    //POST
    server.on("/handshake", HTTP_POST, syncWithHub);  //set IP, PORT, and Epoch
}

bool Wifi::sendPostMessage(const char*url,DynamicJsonDocument doc,int hubPort)
{
  if(!isConnected())
  {
    WARNPRINTLN("Wifi Discconnected, reconnecting now");
    startWifi();
  }

  bool success=true;
  WiFiClient client;
  HTTPClient http;
  char address[128];
    
  // Your Domain name with URL path or IP address with path
  sprintf(address,"http://%s:%d%s",hubAddress,hubPort,url);  
  logger.log(VERBOSE,"Hub Address: %s",address);
  http.begin(client,address);
  http.addHeader("Content-Type", "application/json");

  // Send HTTP POST request
  String buf;
  serializeJson(doc, buf);
  INFOPRINT("POST Content: ");
  INFOPRINTLN(buf);
  int httpResponseCode = http.POST(buf);
  
  if (httpResponseCode<0)
  {
    ERRORPRINT("POST http Code: ");
    VERBOSEPRINT(httpResponseCode);
    VERBOSEPRINT(" - ");
    VERBOSEPRINTLN(http.errorToString(httpResponseCode));

    success=false;
  }
  
  // Free resources
  http.end();

  return success;
}

DynamicJsonDocument Wifi::sendGetMessage(const char*url,int hubPort)
{
  DynamicJsonDocument doc(512);
  WiFiClient client;
  HTTPClient http;
  char address[128];
    
  // Your Domain name with URL path or IP address with path
  sprintf(address,"http://%s:%d%s",hubAddress,hubPort,url);  
  logger.log(VERBOSE,"get Hub Address: %s",address);
  http.begin(client,address);

  // Send HTTP GET request
  int httpResponseCode = http.GET();

  //If it's connection lost, let's retry a couple of times
  int retryCount=0;
  while(httpResponseCode==-5 && retryCount<10) 
  {
    retryCount++;
    logger.log(INFO,"Retrying (Code: %d)",httpResponseCode);
    delay(1000);
    http.begin(client,address);
    httpResponseCode = http.GET();
  }

  //if we're still getting a bad response, then error out
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

  //return payload
  deserializeJson(doc, payload);  
  return doc;
}

void Wifi::sendErrorResponse(const char*errorString)
{
  DynamicJsonDocument doc(48);
  doc["status"] = "KO";
  doc["message"] = errorString;
  
  String buf;
  serializeJson(doc, buf);
  server.send(400,"application/json",buf);
  
  logger.log(ERROR,"Could not stream error response back to client.  No data found, or incorrect!");
}

// Manage not found URL
void Wifi::handleNotFound() 
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
