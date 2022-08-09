#include "Debug.h"
#include "WeatherStation.h"
#include "WindRainHandler.h"
#include "BME280Handler.h"
#include "ADCHandler.h"

#include "Arduino.h"
#include <WiFiClient.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

//globals in ULP which survive deep sleep
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR long millisSinceEpoch = 0;
long epoch=0;  //Epoch from hub

//Wifi
const char* ssid = "WILKIE-LFP";
const char* password = "4777ne178";

//Start web api server
WebServer server(80);

//Handlers
WindRainHandler windRainHandler;
ADCHandler adcHandler;
BME280Handler bmeHandler;


//------------------------------------------------------------

//refresh wind data
void refreshWindRain() 
{
 //wind
 int gustLast12Idx=windRainHandler.getLast12MaxGustIdx();
 DynamicJsonDocument doc(512);
 doc["wind_speed"] = windRainHandler.getWindSpeed();
 doc["wind_direction"] = windRainHandler.getWindDirection();
 doc["wind_gust"] = windRainHandler.getWindGustSpeed();
 doc["wind_gust_max_last12"] = windRainHandler.getLast12MaxGustSpeed(gustLast12Idx);
 doc["wind_gust_direction_last12"] = windRainHandler.getLast12MaxGustDirection(gustLast12Idx);
 doc["wind_gust_max_time"] = windRainHandler.getLast12MaxGustTime(gustLast12Idx);    //whatever the actual time was that the gust ocurred
 doc["current_time"] = epoch+(getSecondsSinceEpoch()); //send back the last epoch sent in + elapsed time since

 //rain
 doc["rain_rate"] = windRainHandler.getRainRate();
 doc["rain_rate_last_hour"] = windRainHandler.getRainRateLastHour();
 doc["rain_max_rate_last12"] = windRainHandler.getMaxRainRateLast12();
 doc["rain_inches_last12"] = windRainHandler.getTotalRainLast12();

 //send wind/rain data back
 String buf;
 serializeJson(doc, buf);
 server.send(200, "application/json", buf);

 INFOPRINTLN("Successfuly refreshed wind and rain data...");
}

//refresh weather data
void refreshWeather() 
{
  DynamicJsonDocument doc(512); 

  doc=refreshBMEDoc(doc);
  doc=refreshADCDoc(doc);
  
  //send bme data back
  String buf;
  serializeJson(doc, buf);
  server.send(200, "application/json", buf);
  
  INFOPRINTLN("Successfuly refreshed general weather data...");
}

//refresh bme data
void refreshBME() 
{
  DynamicJsonDocument doc(512); 

  doc=refreshBMEDoc(doc);
  
  //send bme data back
  String buf;
  serializeJson(doc, buf);
  server.send(200, "application/json", buf);
  
  INFOPRINTLN("Successfuly refreshed BME (temperature, humidity, pressure) data...");
}

//refresh bme data
DynamicJsonDocument refreshBMEDoc(DynamicJsonDocument doc) 
{  
  doc["temperature"] = bmeHandler.getTemperature();
  doc["humidity"] = bmeHandler.getHumidity();
  doc["pressure"] = bmeHandler.getPressure();
  doc["dew_point"] = bmeHandler.getDewPoint();
  doc["heat_index"] = bmeHandler.getHeatIndex();
  doc["temperature_change_last_hour"] = bmeHandler.getTemperatureChange();
  doc["pressure_change_last_hour"] = bmeHandler.getPressureChange();
  doc["temperature_max_last12"] = bmeHandler.getMaxTemperature();
  doc["temperature_max_time_last12"] = bmeHandler.getMaxTemperatureTime();
  doc["temperature_min_last12"] = bmeHandler.getMinTemperature();
  doc["temperature_min_time_last12"] = bmeHandler.getMinTemperatureTime();
  doc["current_time"] = epoch+(getSecondsSinceEpoch()); //send back the last epoch sent in + elapsed time since

  return doc;
}

//refresh adc data
void refreshADC() 
{
  DynamicJsonDocument doc(512); 

  doc=refreshADCDoc(doc);
  
  //send bme data back
  String buf;
  serializeJson(doc, buf);
  server.send(200, "application/json", buf);
  
  INFOPRINTLN("Successfuly refreshed ADC (cap voltage, ldr, moisture, uv) data...");
}

//refresh adc data
DynamicJsonDocument refreshADCDoc(DynamicJsonDocument doc) 
{  
     doc["voltage"] = adcHandler.getVoltage();
     doc["ldr"] = adcHandler.getIllumination();
     doc["moisture"] = adcHandler.getMoisture();
     doc["uv"] = adcHandler.getUV();
     doc["voltage_max_last12"] = adcHandler.getMaxVoltage();
     doc["voltage_min_last12"] = adcHandler.getMinVoltage();
     doc["current_time"] = epoch+(getSecondsSinceEpoch()); //send back the last epoch sent in + elapsed time since

     return doc;
}

//debug data
void debugData()
{
  /*
     INFOPRINTLN("--- BME DEBUG DATA----");
     INFOPRINT("temperature: ");
     INFOPRINTLN(bmeSamples[idx].temperature);
     INFOPRINT("humidity: ");
     INFOPRINTLN(bmeSamples[idx].humidity);
     INFOPRINT("pressure: ");
     INFOPRINTLN(bmeSamples[idx].pressure);
     
     INFOPRINTLN("--- GENERAL DEBUG DATA----");   
     INFOPRINT("Epoch: ");
     INFOPRINTLN(epoch);        
     
     //Web write back
     DynamicJsonDocument doc(512);     

     //Wind
     //doc["windSampleTotal"] = windSampleTotal;

     //BME
     doc["temperature"] = bmeSamples[idx].temperature;
     doc["humidity"] = bmeSamples[idx].humidity;
     doc["pressure"] = bmeSamples[idx].pressure;

     //General
     doc["epoch"] = epoch;
     doc["secondsSinceEpoch"] = getSecondsSinceEpoch();
     
     //send debug data back
     String buf;
     serializeJson(doc, buf);
     server.send(200, "application/json", buf);

     INFOPRINTLN("Successfuly sent debug data...");   
     */  
}

// Serving refresh 
void refreshAll() {
     DynamicJsonDocument doc(512);
   
     //rain
     doc["rain_rate"] = .02;
     doc["rain_rate_last_hour"] = .03;
     doc["rain_max_rate_last12"] = 1.8;
     doc["rain_inches_last12"] = 1.5;

     //environmental quality
     doc["air_quality_pm1"] = 62;
     doc["air_quality_pm25"] = 48;
     doc["air_quality_pm10"] = 73;
     doc["noise_level_max_last12"] = 32;
     doc["noise_level_max_time"] = 1651374875;
     doc["air_quality_pm25_max_last12"] = 55;
     doc["air_quality_pm25_max_time"] = 1651374875;
     doc["air_quality_pm10_max_last12"] = 74;
     doc["air_quality_pm10_max_time"] = 1651374875;
     doc["air_quality_pm25_min_last12"] = 31;
     doc["air_quality_pm25_min_time"] = 1651374875;
     doc["air_quality_pm10_min_last12"] = 41;
     doc["air_quality_pm10_min_time"] = 1651374875;
                   
     INFOPRINTLN("refreshing...");
     String buf;
     serializeJson(doc, buf);
     server.send(200, "application/json", buf);
}

// Serving setting
void getSettings() {
      // Allocate a temporary JsonDocument
      // Don't forget to change the capacity to match your requirements.
      // Use arduinojson.org/v6/assistant to compute the capacity.

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
 
      String buf;
      serializeJson(doc, buf);
      server.send(200, "application/json", buf);
}

//The hub driver will POST epoch when refreshing data
void setEpoch() {
    String postBody = server.arg("plain");

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, postBody);
    if (error) {
        // if the file didn't open, print an error:
        sendErrorResponse("Error parsing json body! <br>" + (String)error.c_str());
        ERRORPRINT("Error parsing JSON: ");
        ERRORPRINTLN(error.c_str());        
        return;
    }
    INFOPRINT("HTTP Method: ");
    INFOPRINTLN(server.method());

    if (server.method() == HTTP_POST) 
    {
        //set epoch from server
        epoch = doc["epoch"].as<long>();
        millisSinceEpoch=0;

        INFOPRINT("Setting epoch to: ");
        INFOPRINTLN(epoch);
       
        // Create the response
        // To get the status of the result you can get the http status so
        // this part can be unusefully
        DynamicJsonDocument doc(512);
        doc["status"] = "OK";
    
        String buf;
        serializeJson(doc, buf); 
        server.send(201,"application/json",buf);
        return;
    }
}

void sendErrorResponse(String errorString)
{
  DynamicJsonDocument doc(512);
  doc["status"] = "KO";
  doc["message"] = "No data found, or incorrect!";
  
  String buf;
  serializeJson(doc, buf);
  server.send(400,"application/json",buf);
  
  ERRORPRINTLN("Streaming error response.  No data found, or incorrect!");
}
 
// Define routing
void restServerRouting() {
    server.on("/", HTTP_GET, []() {
        server.send(200,"text/html",
        "Welcome to the REST Web Server");
    });

    //GET
    server.on("/refreshAll", HTTP_GET, refreshAll);   //deprecated
    server.on("/refreshWindRain", HTTP_GET, refreshWindRain);
    server.on("/refreshWeather", HTTP_GET, refreshWeather);
    server.on("/refreshBME", HTTP_GET, refreshBME);
    server.on("/refreshADC", HTTP_GET, refreshADC);
    server.on("/settings", HTTP_GET, getSettings);    //not currently used 
    server.on("/debug", HTTP_GET, debugData);     //send debug data (also prints to serial

    //POST
    server.on("/setEpoch", HTTP_POST, setEpoch);   //called by the driver on the hub to set epoch
}
 
// Manage not found URL
void handleNotFound() 
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
 
void setup(void) 
{
  if(bootCount>0)
  {
    millisSinceEpoch=millisSinceEpoch+(TIMEDEEPSLEEP*1000);
    INFOPRINT("Millis now: ");
    INFOPRINTLN(millisSinceEpoch);
  }
  
  #if defined(ERRORDEF) || defined(INFODEF) || defined(VERBOSEDEF)
  Serial.begin(115200);
  #endif

  ++bootCount;

  INFOPRINT("Current boot count: ");
  INFOPRINTLN(bootCount); 

  INFOPRINTLN("--------------------------");
  INFOPRINTLN("Starting up");
  
  //free heap
  float heap=(((float)376360-ESP.getFreeHeap())/(float)376360)*100;
  INFOPRINT("Free Heap: (376360 is an empty sketch) ");
  INFOPRINT((int)ESP.getFreeHeap());
  INFOPRINT(" -  ");
  INFOPRINT(heap);
  INFOPRINTLN("% used");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
 
  // Wait for connection
  INFOPRINT("Connecting to WIFI ");  
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    INFOPRINT(".");
  }
  INFOPRINTLN("");
  INFOPRINT("Connected to ");
  INFOPRINT(ssid);
  INFOPRINT("  IP address: ");
  INFOPRINTLN(WiFi.localIP());
 
  // Activate mDNS this is used to be able to connect to the server
  // with local DNS hostmane esp8266.local
  if (MDNS.begin("esp8266")) {
    INFOPRINTLN("MDNS responder started");
  }
  else
  {
    ERRORPRINTLN("ERROR: Unable to start MDNS responder");
  }
 
  // Set server routing and then start
  restServerRouting();
  server.onNotFound(handleNotFound);
  server.begin();
  INFOPRINTLN("HTTP server started");

  //turn off air quality  (make sure sustainable by adding a member function to ULP)
  pinMode(25,OUTPUT);
  digitalWrite(25, LOW); 

  INFOPRINTLN("Ready to go!");
  INFOPRINTLN("");

  INFOPRINTLN("Reading sensors....");
  windRainHandler.storeSamples(TIMEDEEPSLEEP);
  bmeHandler.storeSamples();
  adcHandler.storeSamples();

  millisSinceEpoch=millisSinceEpoch+millis();   //add how long it's been since we last woke up
  sleep();
}
 
void loop(void) 
{
  //take care of webserver stuff
  server.handleClient();
}

void sleep(void) 
{
  INFOPRINTLN("Going to ESP deep sleep for " + String(TIMEDEEPSLEEP) + " seconds...  (night night)");
  
  esp_sleep_enable_timer_wakeup(TIMEDEEPSLEEP * TIMEFACTOR);

  esp_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);

  esp_deep_sleep_start(); 
}

int getSecondsSinceEpoch()
{
  return millisSinceEpoch/1000;
}
