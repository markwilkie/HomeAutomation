#include "MyWifi.h"

WebServer myServer(80);
MyLogger myLogger;

void MyWifi::startWifi()
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

void MyWifi::startServer()
{ 
  // Set server routing and then start
  setupServerRouting();
  //myServer.onNotFound(handleNotFound);
  myServer.begin();
  serverOnFlag=true;
  myLogger.log(INFO,"HTTP server started");  
}

bool MyWifi::isServerOn()
{
  return serverOnFlag;
}

void MyWifi::disableWifi()
{ 
  myLogger.log(VERBOSE,"Disabling Wifi...");  

  //Let's be sure and flush the log cache before shutting down
  myLogger.sendLogs(isConnected());
  
  WiFi.disconnect(true);  // Disconnect from the network
  WiFi.mode(WIFI_OFF);    // Switch WiFi off
  esp_wifi_stop();
  serverOnFlag=false;
}

bool MyWifi::isConnected()
{
  return (WiFi.status() == WL_CONNECTED);
}

int MyWifi::getRSSI()
{
  return WiFi.RSSI();
}

void MyWifi::listen(long secondsToWait)
{
  //take some time to care of webserver stuff  (this will be negotiated in the future)
  if(secondsToWait==0)
  {
    myServer.handleClient();
    ArduinoOTA.handle();
    return;
  }    

  unsigned long startMillis=millis();
  while(millis()<(startMillis+(secondsToWait*1000L)))
  {
    myServer.handleClient();
    ArduinoOTA.handle();
  }

  //It's been a bit, so let's send any logs we have
  myLogger.sendLogs(isConnected());
}

void MyWifi::sendResponse(DynamicJsonDocument doc)
{
  String buf;
  serializeJson(doc, buf);
  myServer.send(200, "application/json", buf);
}

DynamicJsonDocument MyWifi::readContent()
{
  String postBody = myServer.arg("plain");
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, postBody);
  if (error) 
  {
      // if the file didn't open, print an error:
      sendErrorResponse(error.c_str());
      myLogger.log(ERROR,"Problem parsing JSON: %s",error.c_str());      
  }

  return doc;
}

bool MyWifi::isPost()
{
  return (myServer.method() == HTTP_POST);
}

// Define routing
void MyWifi::setupServerRouting() {
    myServer.on("/", HTTP_GET, []() {
        myServer.send(200,"text/html",
        "Welcome to the REST Web Server");
    });

    //GET
    //myServer.on("/settings", HTTP_GET, getSettings);    //not currently used 

    //POST
    myServer.on("/handshake", HTTP_POST, syncWithHub);  //set IP, PORT, and Epoch
}

bool MyWifi::sendPostMessage(const char*url,DynamicJsonDocument doc,int hubPort)
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
  myLogger.log(VERBOSE,"Hub Address: %s",address);
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

bool MyWifi::sendPutMessage(const char*fullurl,const char*token,const char*buf)
{
  if(!isConnected())
  {
    WARNPRINTLN("Wifi Discconnected, reconnecting now");
    startWifi();
  }

  bool success=true;
  //WiFiClient client;
  HTTPClient http;
    
  // Your Domain name with URL path or IP address with path
  myLogger.log(VERBOSE,"PUT Address: %s",fullurl);
  //http.begin(client,fullurl);
  http.begin(fullurl);
  //http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", token);
  //http.addHeader("Host","api.rach.io");

  // Send HTTP PUT request
  INFOPRINT("PUT Content: ");
  INFOPRINTLN(buf);
  int httpResponseCode = http.PUT(buf);
  if (httpResponseCode<0)
  {
    ERRORPRINT("PUT http Code: ");
    VERBOSEPRINT(httpResponseCode);
    VERBOSEPRINT(" - ");
    VERBOSEPRINTLN(http.errorToString(httpResponseCode));

    success=false;
  }
  else
  {
    VERBOSEPRINT("PUT http Code: ");
    VERBOSEPRINT(httpResponseCode);
    VERBOSEPRINT(" - ");
    VERBOSEPRINTLN(http.errorToString(httpResponseCode));
  }
  
  // Free resources
  http.end();

  return success;
}

DynamicJsonDocument MyWifi::sendGetMessage(const char*url,int hubPort)
{
  DynamicJsonDocument doc(512);
  WiFiClient client;
  HTTPClient http;
  char address[128];
    
  // Your Domain name with URL path or IP address with path
  sprintf(address,"http://%s:%d%s",hubAddress,hubPort,url);  
  myLogger.log(VERBOSE,"get Hub Address: %s",address);
  http.begin(client,address);

  // Send HTTP GET request
  int httpResponseCode = http.GET();

  //If it's connection lost, let's retry a couple of times
  int retryCount=0;
  while(httpResponseCode==-5 && retryCount<10) 
  {
    retryCount++;
    myLogger.log(INFO,"Retrying (Code: %d)",httpResponseCode);
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

void MyWifi::sendErrorResponse(const char*errorString)
{
  DynamicJsonDocument doc(48);
  doc["status"] = "KO";
  doc["message"] = errorString;
  
  String buf;
  serializeJson(doc, buf);
  myServer.send(400,"application/json",buf);
  
  myLogger.log(ERROR,"Could not stream error response back to client.  No data found, or incorrect!");
}

// Manage not found URL
void MyWifi::handleNotFound() 
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += myServer.uri();
  message += "\nMethod: ";
  message += (myServer.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += myServer.args();
  message += "\n";
  for (uint8_t i = 0; i < myServer.args(); i++) {
    message += " " + myServer.argName(i) + ": " + myServer.arg(i) + "\n";
  }
  myServer.send(404, "text/plain", message);

  myLogger.log(ERROR,"404 - %s",message.c_str());
}

DynamicJsonDocument MyWifi::readJsonFile(char* url)
{
  DynamicJsonDocument doc(2048);
  HTTPClient http;
  
  if(http.begin(url)) 
  {
    int httpCode = http.GET();
    
    if (httpCode > 0) 
    {
      // file found at server
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) 
      {
        String payload = http.getString();
        deserializeJson(doc, payload);               
      }
    } 
    else 
    {
      myLogger.log(ERROR,"File GET failed. Error: %s\n", http.errorToString(httpCode).c_str());
    }
    
    http.end();
    return doc;
  }
}
