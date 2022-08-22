#include "WeatherWifi.h"
#include "debug.h"

WebServer server(80);

void WeatherWifi::startWifi()
{
  esp_wifi_start();
  WiFi.disconnect(false);  // Reconnect the network
  WiFi.mode(WIFI_STA);    // Switch WiFi on
  INFOPRINTLN("Wifi enabled...");
  blinkLED(4,50,50);
  
  //Setup wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
 
  // Wait for connection
  int connectCount = 0;
  INFOPRINT("Connecting to WIFI ");  
  while (WiFi.status() != WL_CONNECTED) 
  {
    blinkLED(4,200,50);
    connectCount++;
    delay(500);
    INFOPRINT(".");
    if(connectCount>40)
    {
      blinkLED(20,10,10);
      ERRORPRINTLN("Could not connect to Wifi, restarting board");
      ESP.restart();
    }
  }

  INFOPRINTLN("");
  INFOPRINT("Connected to ");
  INFOPRINT(SSID);
  INFOPRINT("  IP address: ");
  INFOPRINTLN(WiFi.localIP());
}

void WeatherWifi::startServer()
{ 
  // Set server routing and then start
  setupServerRouting();
  //server.onNotFound(handleNotFound);
  server.begin();
  INFOPRINTLN("HTTP server started");  
}

void WeatherWifi::disableWifi()
{ 
  WiFi.disconnect(true);  // Disconnect from the network
  WiFi.mode(WIFI_OFF);    // Switch WiFi off
  esp_wifi_stop();
  INFOPRINTLN("Wifi disabled...");  
}

bool WeatherWifi::isConnected()
{
  return (WiFi.status() == WL_CONNECTED);
}

void WeatherWifi::listen(long millisToWait)
{
  //take some time to care of webserver stuff  (this will be negotiated in the future)
  VERBOSEPRINTLN("Listening on http");
  long startMillis=millis();
  while(millis()<(startMillis+millisToWait))
    server.handleClient();
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
      ERRORPRINT("Error parsing JSON: ");
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

void WeatherWifi::sendPostMessage(String url,DynamicJsonDocument doc,int hubPort)
{
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
    ERRORPRINT("Error from POST Code: ");
    ERRORPRINTLN(httpResponseCode);
    handshakeRequired=true;
  }
  
  // Free resources
  http.end();
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
    ERRORPRINT("Error from GET Code: ");
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
  
  ERRORPRINTLN("Streaming error response.  No data found, or incorrect!");
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

  ERRORPRINT("404 Error: ");
  ERRORPRINTLN(message);
}
