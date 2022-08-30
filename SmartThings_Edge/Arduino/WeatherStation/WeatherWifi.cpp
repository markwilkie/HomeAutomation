#include "WeatherWifi.h"

WebServer server(80);
Logger logger;

void WeatherWifi::startWifi()
{
  serverOnFlag=false;  //clearly, the server is not yet on

  esp_wifi_start();
  WiFi.disconnect(false);  // Reconnect the network
  WiFi.mode(WIFI_STA);    // Switch WiFi on
  INFOPRINTLN("Wifi enabled...");
  
  //Setup wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
 
  // Wait for connection
  int connectCount = 0;
  INFOPRINT("Connecting to WIFI ");  
  while (WiFi.status() != WL_CONNECTED) 
  {
    connectCount++;
    delay(500);
    INFOPRINT(".");
    if(connectCount>40)
    {
      //Blink because we couldn't connect to wifi  (5x250)
      blinkLED(5,250,250);

      ERRORPRINTLN("ERROR: Could not connect to Wifi, restarting board");
      ESP.restart();
    }
  }

  INFOPRINTLN("");
  INFOPRINT("Connected to ");
  INFOPRINT(SSID);
  INFOPRINT("  IP address: ");
  INFOPRINTLN(WiFi.localIP().toString());

  //Init OTA
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      INFOPRINTLN("Starting OTA updating ");
    })
    .onEnd([]() {
      INFOPRINTLN("\nEnd OTA");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      ERRORPRINT("ERROR: OTA Problem: ");
      ERRORPRINTLN(error);
      if (error == OTA_AUTH_ERROR) { ERRORPRINTLN("Auth Failed"); }
      else if (error == OTA_BEGIN_ERROR){ ERRORPRINTLN("Begin Failed"); }
      else if (error == OTA_CONNECT_ERROR){ ERRORPRINTLN("Connect Failed"); }
      else if (error == OTA_RECEIVE_ERROR) { ERRORPRINTLN("Receive Failed"); }
       else if (error == OTA_END_ERROR) { ERRORPRINTLN("End Failed"); }
    });

  ArduinoOTA.begin();

  //Init logger and send logs if defined
  #ifdef WIFILOGGER 
    logger.init();
    logger.sendLogs();
  #endif
}

void WeatherWifi::startServer()
{ 
  // Set server routing and then start
  setupServerRouting();
  //server.onNotFound(handleNotFound);
  server.begin();
  serverOnFlag=true;
  INFOPRINTLN("HTTP server started");  
}

bool WeatherWifi::isServerOn()
{
  return serverOnFlag;
}

void WeatherWifi::disableWifi()
{ 
  //First, let's send logs
  #ifdef WIFILOGGER 
    logger.sendLogs();
  #endif
  
  WiFi.disconnect(true);  // Disconnect from the network
  WiFi.mode(WIFI_OFF);    // Switch WiFi off
  esp_wifi_stop();
  serverOnFlag=false;
  INFOPRINTLN("Wifi disabled...");  
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
  VERBOSEPRINTLN("Listening on http and OTA");
  long startMillis=millis();
  while(millis()<(startMillis+millisToWait))
  {
    server.handleClient();
    ArduinoOTA.handle();
  }

  //It's been a bit, so let's send any logs we have
  #ifdef WIFILOGGER 
    logger.sendLogs();
  #endif    
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
      ERRORPRINT("ERROR: Problem parsing JSON: ");
      ERRORPRINTLN(error.c_str());        
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
  INFOPRINTLN("Hub Address: "+address);
  http.begin(client,address.c_str());
  http.addHeader("Content-Type", "application/json");

  // Send HTTP POST request
  String buf;
  serializeJson(doc, buf);
  INFOPRINTLN("Content: "+buf);
  int httpResponseCode = http.POST(buf);
  
  if (httpResponseCode<0)
  {
    ERRORPRINT("ERROR: POST http Code: ");
    ERRORPRINTLN(httpResponseCode);
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
  INFOPRINTLN("Hub Address: "+address);
  http.begin(client,address.c_str());

  // Send HTTP GET request
  int httpResponseCode = http.GET();

  if (httpResponseCode<0)
  {
    ERRORPRINT("ERROR: GET http Code: ");
    ERRORPRINTLN(httpResponseCode);
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
  
  ERRORPRINTLN("ERROR: Could not stream error response back to client.  No data found, or incorrect!");
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

  ERRORPRINT("ERROR: 404 ");
  ERRORPRINTLN(message);
}
