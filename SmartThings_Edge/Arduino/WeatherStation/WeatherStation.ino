#include "Debug.h"
#include "ULP.h"
#include "WeatherWifi.h"
#include "WeatherStation.h"
#include "WindRainHandler.h"
#include "BME280Handler.h"
#include "ADCHandler.h"
#include "PMS5003Handler.h"

#include "Arduino.h"
#include <ArduinoJson.h>

//globals in ULP which survive deep sleep
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR bool firstBoot = true;
RTC_DATA_ATTR long millisSinceEpoch = 0;
RTC_DATA_ATTR ULP ulp;

//when true, ESP goes into a high battery drain state of waiting for a /handshake POST message
RTC_DATA_ATTR bool handshakeRequired = true;

//info from hub obtained by handshaking
RTC_DATA_ATTR String hubAddress;
RTC_DATA_ATTR int hubPort;
RTC_DATA_ATTR long epoch;  //Epoch from hub

//Wifi
WeatherWifi weatherWifi;

//Handlers
WindRainHandler windRainHandler;
ADCHandler adcHandler;
BME280Handler bmeHandler;
PMS5003Handler pmsHandler;


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
 weatherWifi.sendResponse(doc);

 INFOPRINTLN("Successfuly refreshed wind and rain data...");
}

//refresh weather data
void postWeather() 
{
  DynamicJsonDocument doc(512); 

  doc=refreshBMEDoc(doc);
  doc=refreshADCDoc(doc);
  
  //send data
  weatherWifi.sendPostMessage("/weather",doc);
  
  INFOPRINTLN("Tried posting general weather data...");
}

//refresh bme data
void refreshBME() 
{
  DynamicJsonDocument doc(512); 

  doc=refreshBMEDoc(doc);
  
  //send bme data back
  weatherWifi.sendResponse(doc);
  
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
  weatherWifi.sendResponse(doc);
  
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

//refresh air quality data
void refreshPMS() 
{
 //pms 5003
 DynamicJsonDocument doc(512);
 doc["pm10"] = pmsHandler.getPM10Standard();
 doc["pm25"] = pmsHandler.getPM25Standard();
 doc["pm100"] = pmsHandler.getPM100Standard();
 
 //send data back
 weatherWifi.sendResponse(doc);

 INFOPRINTLN("Successfuly refreshed PMS 5003...");
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

// Serving setting
void getSettings() 
{
      // Allocate a temporary JsonDocument
      // Don't forget to change the capacity to match your requirements.
      // Use arduinojson.org/v6/assistant to compute the capacity.

      // You can use DynamicJsonDocument as well
      //DynamicJsonDocument doc(512);
 
      //doc["ip"] = WiFi.localIP().toString();
      //doc["gw"] = WiFi.gatewayIP().toString();
      //doc["nm"] = WiFi.subnetMask().toString();
 
      //if (server.arg("signalStrength")== "true"){
      //    doc["signalStrengh"] = WiFi.RSSI();
      //}
      //if (server.arg("freeHeap")== "true"){
      //    doc["freeHeap"] = ESP.getFreeHeap();
      //}
 
      //weatherWifi.sendResponse(doc);
}

//The hub driver will POST epoch when refreshing data
void setEpoch() 
{
  DynamicJsonDocument doc=weatherWifi.readContent();

  if (weatherWifi.isPost()) 
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
        weatherWifi.sendResponse(doc);
    }
}

//The hub driver will POST ip, port, and epoch when handshaking
void syncWithHub() 
{
  DynamicJsonDocument doc=weatherWifi.readContent();

  if (weatherWifi.isPost()) 
  {
      hubAddress = doc["hubAddress"].as<String>();
      hubPort = doc["hubPort"].as<int>();
      epoch = doc["epoch"].as<long>();
      millisSinceEpoch=0;

      INFOPRINT("Hub info: ");
      INFOPRINT(hubAddress + ":" + hubPort);
      INFOPRINT("  Epoch: ");
      INFOPRINTLN(epoch);

      //handshake no longer needed
      handshakeRequired = false;
     
      // Create the response
      // To get the status of the result you can get the http status so
      // this part can be unusefully
      DynamicJsonDocument doc(512);
      doc["status"] = "OK";   
      weatherWifi.sendResponse(doc);
  }
}

void setup(void) 
{ 
  #if defined(ERRORDEF) || defined(INFODEF) || defined(VERBOSEDEF)
  Serial.begin(115200);
  #endif

  INFOPRINTLN("--------------------------");
  INFOPRINTLN("Starting up");

  ++bootCount;
  INFOPRINT("Current boot count: ");
  INFOPRINTLN(bootCount); 

  if(firstBoot)
  {
    INFOPRINTLN("Setting up ULP so we can count pulses and hold pins while in deep sleep...  (only does this on first boot)");
    ulp.setupULP();
    ulp.setupWindPin();
    ulp.setupRainPin();
    ulp.setupAirPin();

    firstBoot=false;
  }
    
  if(!firstBoot)
  {
    millisSinceEpoch=millisSinceEpoch+(TIMEDEEPSLEEP*1000);
    INFOPRINT("Millis now: ");
    INFOPRINTLN(millisSinceEpoch);
  }  
  
  //free heap
  float heap=(((float)376360-ESP.getFreeHeap())/(float)376360)*100;
  INFOPRINT("Free Heap: (376360 is an empty sketch) ");
  INFOPRINT((int)ESP.getFreeHeap());
  INFOPRINT(" -  ");
  INFOPRINT(heap);
  INFOPRINTLN("% used");

  INFOPRINTLN("Init sensors....");
  windRainHandler.init();
  bmeHandler.init();
  adcHandler.init();
  pmsHandler.init();

  INFOPRINTLN("turning on PMS5003....");
  ulp.setAirPinHigh(true);

  // Activate mDNS this is used to be able to connect to the server
  // with local DNS hostmane esp8266.local
  if (MDNS.begin("esp8266")) {
    INFOPRINTLN("MDNS responder started");
  }
  else
  {
    ERRORPRINTLN("ERROR: Unable to start MDNS responder");
  }

  //wifi
  weatherWifi.initWifi();
  weatherWifi.startServer();
 
  INFOPRINTLN("Ready to go!");
  INFOPRINTLN("");
}
 
void loop(void) 
{
  //Read Sensors
  readSensors();

  //POST sensor data
  postWeather();

  //Check in w/ web server
  weatherWifi.listen(10000);

  //Keep track of how long
  millisSinceEpoch=millisSinceEpoch+(millis()-epoch);   //add how long it's been since we last woke up
  //sleep();    
}

void readSensors()
{
  INFOPRINTLN("Reading sensors....");
  windRainHandler.storeSamples(TIMEDEEPSLEEP);
  bmeHandler.storeSamples();
  adcHandler.storeSamples();
  pmsHandler.storeSamples();
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
