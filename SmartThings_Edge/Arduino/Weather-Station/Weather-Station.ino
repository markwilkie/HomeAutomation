/*
 *  Simple hello world Json REST response
  *  by Mischianti Renzo <https://www.mischianti.org>
 *
 *  https://www.mischianti.org/
 *
 */
 
#include "Arduino.h"
#include <WiFiClient.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
 
const char* ssid = "WILKIE-LFP";
const char* password = "4777ne178";
 
WebServer server(80);

// Serving refresh 
void refreshAll() {
     DynamicJsonDocument doc(512);
      doc["wind_speed"] = 6.3;
      doc["wind_direction"] = 180;
      doc["wind_gust"] = 16.2;
      doc["rain"] = .02;
      doc["temp"] = 46;
      doc["humidity"] = 65;
      doc["dew"] = 44;
      doc["pressure"] = 22;
     
      Serial.println(F("refreshing..."));
      String buf;
      serializeJson(doc, buf);
      server.send(200, "application/json", buf);
}
 
// Serving wind speed
void getWindSpeed() {
     DynamicJsonDocument doc(512);
      doc["speed"] = 13.2;
 
      Serial.print(F("Streaming..."));
      String buf;
      serializeJson(doc, buf);
      server.send(200, "application/json", buf);
      Serial.println(F("done."));
}

// Serving setting
void getSettings() {
      // Allocate a temporary JsonDocument
      // Don't forget to change the capacity to match your requirements.
      // Use arduinojson.org/v6/assistant to compute the capacity.
    //  StaticJsonDocument<512> doc;
      // You can use DynamicJsonDocument as well
      DynamicJsonDocument doc(512);
 
      doc["ip"] = WiFi.localIP().toString();
      doc["gw"] = WiFi.gatewayIP().toString();
      doc["nm"] = WiFi.subnetMask().toString();
 
      if (server.arg("signalStrength")== "true"){
          doc["signalStrengh"] = WiFi.RSSI();
      }


      if (server.arg("freeHeap")== "true"){
          doc["freeHeap"] = ESP.getFreeHeap();
      }
 
      Serial.print(F("Stream..."));
      String buf;
      serializeJson(doc, buf);
      server.send(200, F("application/json"), buf);
      Serial.print(F("done."));
}

void setWindCalibration() {
    String postBody = server.arg("plain");

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, postBody);
    if (error) {
        // if the file didn't open, print an error:
        Serial.print(F("Error parsing JSON "));
        Serial.println(error.c_str());
        sendErrorResponse("Error parsing json body! <br>" + (String)error.c_str());
        return;
    }
    
    JsonObject postObj = doc.as<JsonObject>();

    Serial.print(F("HTTP Method: "));
    Serial.println(server.method());

    if (server.method() == HTTP_POST) {
        if (!postObj.containsKey("name") || !postObj.containsKey("type")) {
            sendErrorResponse("No data found, or incorrect!");
            return;
        }

        Serial.println(F("Parsed incoming POST command"));
       
    
        // Create the response
        // To get the status of the result you can get the http status so
        // this part can be unusefully
        DynamicJsonDocument doc(512);
        doc["status"] = "OK";
    
        Serial.print(F("Streaming..."));
        String buf;
        serializeJson(doc, buf);
    
        server.send(201, F("application/json"), buf);
        Serial.println(F("done."));
        return;
    }
}

void sendErrorResponse(String errorString)
{
  DynamicJsonDocument doc(512);
  doc["status"] = "KO";
  doc["message"] = F("No data found, or incorrect!");
  
  Serial.print(F("Streaming error response..."));
  String buf;
  serializeJson(doc, buf);
  
  server.send(400, F("application/json"), buf);
  Serial.println(F("done."));
}
 
// Define routing
void restServerRouting() {
    server.on("/", HTTP_GET, []() {
        server.send(200, F("text/html"),
            F("Welcome to the REST Web Server"));
    });

    //GET
    server.on(F("/refresh"), HTTP_GET, refreshAll);
    server.on(F("/windSpeed"), HTTP_GET, getWindSpeed);
    server.on(F("/settings"), HTTP_GET, getSettings);    

    //POST
    server.on(F("/setWindCalibration"), HTTP_POST, setWindCalibration);
}
 
// Manage not found URL
void handleNotFound() {
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
}
 
void setup(void) {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");
 
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
 
  // Activate mDNS this is used to be able to connect to the server
  // with local DNS hostmane esp8266.local
  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started");
  }
 
  // Set server routing
  restServerRouting();
  // Set not found response
  server.onNotFound(handleNotFound);
  // Start server
  server.begin();
  Serial.println("HTTP server started");
}
 
void loop(void) {
  server.handleClient();
}
