#include "WeatherStation.h"
#include "WindHandler.h"

//Wifi
const char* ssid = "WILKIE-LFP";
const char* password = "4777ne178";

//Start web api server
WebServer server(80);

//Timer vars
volatile long secondsSinceEpoch;
hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

//Timer ISR, called once a second
void IRAM_ATTR onTimer() 
{
    //Increment main epoch closk
    portENTER_CRITICAL_ISR(&timerMux);
    secondsSinceEpoch++;
    portEXIT_CRITICAL_ISR(&timerMux);

    //Check if we need to store anemometer sample
    windSeconds++;
    if(windSeconds>=WIND_SAMPLE_SEC)
    {
      windSeconds=0;
      storeWindSample();
    }
}

//------------------------------------------------------------


//refresh wind data
void refreshWind() {
     DynamicJsonDocument doc(512);
     
     //wind
     doc["wind_speed"] = convertToKTS(windSampleTotal/WIND_PERIOD_SIZE);
     doc["wind_direction"] = 180;
     doc["wind_gust"] = convertToKTS(gustMax);
     doc["wind_gust_max_last12"] = convertToKTS(gustLast12[getLast12MaxGustIndex()]);
     doc["wind_gust_max_time"] = gustLast12Time[getLast12MaxGustIndex()];    //whatever the actual time was that the gust ocurred
     doc["current_time"] = epoch+secondsSinceEpoch; //send back the last epoch sent in + elapsed time since

     //send wind data back
     Serial.println(F("Refreshing wind data..."));
     String buf;
     serializeJson(doc, buf);
     server.send(200, "application/json", buf);
}

// Serving refresh 
void refreshAll() {
     DynamicJsonDocument doc(512);

     //temperature/humidity
     doc["temp"] = 57;
     doc["humidity"] = 45;
     doc["dew"] = 44;
     doc["pressure"] = 29.98;
     doc["temp_max_last12"] = 63;
     doc["temp_max_time"] = 1651374875;
     doc["temp_min_last12"] = 44;
     doc["temp_min_time"] = 1651374875;
     
     //rain
     doc["rain_rate"] = .02;
     doc["rain_rate_last_hour"] = .03;
     doc["rain_max_rate_last12"] = 1.8;
     doc["rain_inches_last12"] = 1.5;

     //sun
     doc["sun_intensity"] = 32;
     doc["sun_uv"] = 4;
     doc["sun_intensity_max_last12"] = .02;
     doc["sun_intensity_max_time"] = 1651374875;
     doc["sun_uv_max_last12"] = 64;
     doc["sun_uv_max_time"] = 1651374875;

     //environmental quality
     doc["noise_level"] = 32;
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

     //station health
     doc["cap1_voltage"] = 2.6;
     doc["cap2_voltage"] = 2.5;
     doc["solar_voltage"] = 12.1;
     doc["solar_voltage_max_last12"] = 12.6;
     doc["solar_voltage_max_time"] = 1651374875;
     doc["solar_voltage_min_last12"] = 11.4;
     doc["solar_voltage_min_time"] = 1651374875;
                    
     Serial.println(F("refreshing..."));
     String buf;
     serializeJson(doc, buf);
     server.send(200, "application/json", buf);
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

//The hub driver will POST epoch when refreshing data
void setEpoch() {
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
    Serial.print(F("HTTP Method: "));
    Serial.println(server.method());

    if (server.method() == HTTP_POST) {
        //set epoch from server
        epoch = doc["epoch"].as<long>();

        portENTER_CRITICAL_ISR(&timerMux);
        secondsSinceEpoch=0;
        portEXIT_CRITICAL_ISR(&timerMux);

        Serial.print(F("Setting epoch to: "));
        Serial.println(epoch);
       
        // Create the response
        // To get the status of the result you can get the http status so
        // this part can be unusefully
        DynamicJsonDocument doc(512);
        doc["status"] = "OK";
    
        String buf;
        serializeJson(doc, buf); 
        server.send(201, F("application/json"), buf);
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
    server.on(F("/refreshAll"), HTTP_GET, refreshAll);   //deprecated
    server.on(F("/refreshWind"), HTTP_GET, refreshWind);
    server.on(F("/settings"), HTTP_GET, getSettings);    //not currently used 

    //POST
    server.on(F("/setEpoch"), HTTP_POST, setEpoch);   //called by the driver on the hub to set epoch
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

  //Start timer to count time in seconds
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 1000000, true);
  timerAlarmEnable(timer);  

  //Setup pulse counters
  initWindPulseCounter();
}
 
void loop(void) {
  server.handleClient();
}
