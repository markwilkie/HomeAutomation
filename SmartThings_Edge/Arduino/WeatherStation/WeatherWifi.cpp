#include "WeatherWifi.h"
#include "debug.h"

WebServer server(80);

void WeatherWifi::initWifi()
{
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

void WeatherWifi::listen(long millisToWait)
{
  //take some time to care of webserver stuff  (this will be negotiated in the future)
  INFOPRINTLN("Now listening on http....");
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
    server.on("/refreshWindRain", HTTP_GET, refreshWindRain);
    server.on("/refreshWind", HTTP_GET, refreshWindRain);
    server.on("/refreshBME", HTTP_GET, refreshBME);
    server.on("/refreshADC", HTTP_GET, refreshADC);
    server.on("/refreshPMS", HTTP_GET, refreshPMS);
    server.on("/settings", HTTP_GET, getSettings);    //not currently used 
    server.on("/debug", HTTP_GET, debugData);     //send debug data (also prints to serial

    //POST
    server.on("/setEpoch", HTTP_POST, setEpoch);   //called by the driver on the hub to set epoch
    server.on("/handshake", HTTP_POST, syncWithHub);  //set IP, PORT, and Epoch
}

void WeatherWifi::sendPostMessage(DynamicJsonDocument doc)
{
  WiFiClient client;
  HTTPClient http;
    
  // Your Domain name with URL path or IP address with path
  http.begin(client, hubAddress, hubPort);

  // Send HTTP POST request
  String buf;
  serializeJson(doc, buf);
  int httpResponseCode = http.POST(buf);
  
  if (httpResponseCode>0) {
    INFOPRINTLN("HTTP Response to Post: ");
    INFOPRINTLN(httpResponseCode);
  }
  else {
    ERRORPRINT("Error from POST Code: ");
    ERRORPRINTLN(httpResponseCode);
  }
  // Free resources
  http.end();
}

void WeatherWifi::sendErrorResponse(String errorString)
{
  DynamicJsonDocument doc(512);
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
