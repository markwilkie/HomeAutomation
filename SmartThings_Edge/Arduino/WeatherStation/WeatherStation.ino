#include "Debug.h"
#include "WeatherStation.h"
#include "WindHandler.h"
#include "BME280Handler.h"
#include "EnvironmentCalculations.h"
#include "ADCHandler.h"

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
    windSeconds++;
    bmeSeconds++;
    adcSeconds++;
    portEXIT_CRITICAL_ISR(&timerMux); 
}

//------------------------------------------------------------


//refresh wind data
void refreshWind() 
{  
     //wind
     int gustLast12Idx=getLast12MaxGustIndex();
     DynamicJsonDocument doc(512);
     doc["wind_speed"] = convertToKTS(windSampleTotal/WIND_PERIOD_SIZE);
     doc["wind_direction"] = windDirection;
     doc["wind_gust"] = convertToKTS(gustMax);
     doc["wind_gust_max_last12"] = convertToKTS(gustLast12[gustLast12Idx].gust);
     doc["wind_gust_direction_last12"] = gustLast12[gustLast12Idx].gustDirection;
     doc["wind_gust_max_time"] = gustLast12[gustLast12Idx].gustTime;    //whatever the actual time was that the gust ocurred

     //send wind data back
     String buf;
     serializeJson(doc, buf);
     server.send(200, "application/json", buf);

     VERBOSEPRINTLN("Successfuly refreshed wind data...");
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
  
  VERBOSEPRINTLN("Successfuly refreshed general weather data...");
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
  
  VERBOSEPRINTLN("Successfuly refreshed BME (temperature, humidity, pressure) data...");
}

//refresh bme data
DynamicJsonDocument refreshBMEDoc(DynamicJsonDocument doc) 
{  
  //bme
  int idx=bmeSampleIdx-1;
  if(idx<0)
  idx=0;
  
  doc["temperature"] = bmeSamples[idx].temperature;
  doc["humidity"] = bmeSamples[idx].humidity;
  doc["pressure"] = bmeSamples[idx].pressure;
  doc["dew_point"] = EnvironmentCalculations::DewPoint(bmeSamples[idx].temperature, bmeSamples[idx].humidity, EnvironmentCalculations::TempUnit_Celsius);
  doc["heat_index"] = EnvironmentCalculations::HeatIndex(bmeSamples[idx].temperature, bmeSamples[idx].humidity, EnvironmentCalculations::TempUnit_Celsius);;
  doc["temperature_max_last12"] = getMaxTemperature();
  doc["temperature_min_last12"] = getMinTemperature();
  doc["current_time"] = epoch+secondsSinceEpoch; //send back the last epoch sent in + elapsed time since

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
  
  VERBOSEPRINTLN("Successfuly refreshed ADC (cap voltage, ldr, moisture, uv) data...");
}

//refresh adc data
DynamicJsonDocument refreshADCDoc(DynamicJsonDocument doc) 
{  
     //adc
     int idx=adcSampleIdx-1;
     if(idx<0)
      idx=0;
      
     doc["cap_voltage"] = adcSamples[idx].capVoltage;
     doc["ldr"] = adcSamples[idx].ldr;
     doc["moisture"] = adcSamples[idx].moisture;
     doc["uv"] = adcSamples[idx].u);
     doc["cap_voltage_max_last12"] = getMaxCapVoltage();
     doc["cap_voltage_min_last12"] = getMinCapVoltage();
     doc["current_time"] = epoch+secondsSinceEpoch; //send back the last epoch sent in + elapsed time since

     return doc;
}

//debug data
void debugData()
{
     //Serial write
     VERBOSEPRINTLN("--- WIND DEBUG DATA----");    
     VERBOSEPRINT("Running wind sample total: ");
     VERBOSEPRINTLN(windSampleTotal);  

     VERBOSEPRINTLN("--- BME DEBUG DATA----");
     int idx=bmeSampleIdx-1;
     if(idx<0)
      idx=0;
     VERBOSEPRINT("temperature: ");
     VERBOSEPRINTLN(bmeSamples[idx].temperature);
     VERBOSEPRINT("humidity: ");
     VERBOSEPRINTLN(bmeSamples[idx].humidity);
     VERBOSEPRINT("pressure: ");
     VERBOSEPRINTLN(bmeSamples[idx].pressure);
     
     VERBOSEPRINTLN("--- GENERAL DEBUG DATA----");   
     VERBOSEPRINT("Epoch: ");
     VERBOSEPRINTLN(epoch);        
     
     //Web write back
     DynamicJsonDocument doc(512);     

     //Wind
     doc["windSampleTotal"] = windSampleTotal;

     //BME
     doc["temperature"] = bmeSamples[idx].temperature;
     doc["humidity"] = bmeSamples[idx].humidity;
     doc["pressure"] = bmeSamples[idx].pressure;

     //General
     doc["epoch"] = epoch;
     doc["secondsSinceEpoch"] = secondsSinceEpoch;
     
     //send debug data back
     String buf;
     serializeJson(doc, buf);
     server.send(200, "application/json", buf);

     VERBOSEPRINTLN("Successfuly sent debug data...");     
}

// Serving refresh 
void refreshAll() {
     DynamicJsonDocument doc(512);
   
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
                    
     VERBOSEPRINTLN("refreshing...");
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
    VERBOSEPRINT("HTTP Method: ");
    VERBOSEPRINTLN(server.method());

    if (server.method() == HTTP_POST) 
    {
        //set epoch from server
        epoch = doc["epoch"].as<long>();

        portENTER_CRITICAL_ISR(&timerMux);
        secondsSinceEpoch=0;
        portEXIT_CRITICAL_ISR(&timerMux);

        VERBOSEPRINT("Setting epoch to: ");
        VERBOSEPRINTLN(epoch);
       
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
    server.on("/refreshWind", HTTP_GET, refreshWind);
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
  #if defined(ERRORDEF) || defined(VERBOSEDEF)
  Serial.begin(115200);
  #endif

  VERBOSEPRINTLN("--------------------------");
  VERBOSEPRINTLN("Starting up");

  //init data structures
  initWindDataStructures();
  
  //free heap
  float heap=(((float)376360-ESP.getFreeHeap())/(float)376360)*100;
  VERBOSEPRINT("Free Heap: (376360 is an empty sketch) ");
  VERBOSEPRINTLN((int)ESP.getFreeHeap());
  VERBOSEPRINT(heap);
  VERBOSEPRINTLN("% used");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

 
  // Wait for connection
  VERBOSEPRINT("Connecting to WIFI ");  
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    VERBOSEPRINT(".");
  }
  VERBOSEPRINTLN("");
  VERBOSEPRINT("Connected to ");
  VERBOSEPRINTLN(ssid);
  VERBOSEPRINT("IP address: ");
  VERBOSEPRINTLN(WiFi.localIP());
 
  // Activate mDNS this is used to be able to connect to the server
  // with local DNS hostmane esp8266.local
  if (MDNS.begin("esp8266")) {
    VERBOSEPRINTLN("MDNS responder started");
  }
  else
  {
    ERRORPRINTLN("ERROR: Unable to start MDNS responder");
  }
 
  // Set server routing and then start
  restServerRouting();
  server.onNotFound(handleNotFound);
  server.begin();
  VERBOSEPRINTLN("HTTP server started");

  //Start timer to count time in seconds
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 1000000, true);
  timerAlarmEnable(timer);  

  //Setup sensors
  VERBOSEPRINTLN("Setting up sensors...");
  initWindPulseCounter();
  setupBME();
  setupADC();

  //turn off air quality
  pinMode(25,OUTPUT);
  digitalWrite(25, LOW); 

  VERBOSEPRINTLN("Ready to go!");
  VERBOSEPRINTLN("");
}
 
void loop(void) 
{
  //take care of webserver stuff
  server.handleClient();

  //Check if we need to store anemometer sample
  if(windSeconds>=WIND_SAMPLE_SEC)
  {
    windSeconds=0;
    storeWindSample();
    storeWindDirection();
  }

  //take care of webserver stuff before too much time goes by
  yield();
  server.handleClient();
    
  //Check if we need to store BME280 sample
  if(bmeSeconds>=BME_SAMPLE_SEC)
  {
    bmeSeconds=0;
    storeBMESample();
  }

  //take care of webserver stuff before too much time goes by
  yield();
  server.handleClient();
    
  //Check if we need to store ADC samples
  if(adcSeconds>=ADC_SAMPLE_SEC)
  {
    adcSeconds=0;
    storeADCSample();
  }  
}
