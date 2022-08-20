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
RTC_DATA_ATTR bool firstBoot = true;
RTC_DATA_ATTR ULP ulp;

//when true, ESP goes into a high battery drain state of waiting for a /handshake POST message
RTC_DATA_ATTR bool handshakeRequired = true;

//info from hub obtained by handshaking
RTC_DATA_ATTR char hubAddress[30];
RTC_DATA_ATTR int hubWindPort=0;
RTC_DATA_ATTR int hubWeatherPort=0;
RTC_DATA_ATTR int hubRainPort=0;
RTC_DATA_ATTR long epoch=0;  //Epoch from hub
RTC_DATA_ATTR long timeToPost = 0;  //seconds for when to http POST

//when to read sensors
RTC_DATA_ATTR long timeToReadSensors=0;   //default
RTC_DATA_ATTR long timeToReadWind=0;      //wind  (will be on a greater cadence)
RTC_DATA_ATTR long timeToReadAir=0;       //air quality sensor which we'll rarely read
RTC_DATA_ATTR bool airWarmedUp=false;     //air quality sensor will warm up for a full sleep cycle

//time keeping
long millisAtEpoch = 0;

//Wifi
WeatherWifi weatherWifi;

//Handlers
RTC_DATA_ATTR WindRainHandler windRainHandler;
RTC_DATA_ATTR ADCHandler adcHandler;
RTC_DATA_ATTR BME280Handler bmeHandler;
RTC_DATA_ATTR PMS5003Handler pmsHandler;


//------------------------------------------------------------

//refresh wind data
void postWind() 
{
 //wind
 DynamicJsonDocument doc(512);
 doc["wind_speed"] = windRainHandler.getWindSpeed();
 doc["wind_direction"] = windRainHandler.getWindDirection();
 doc["wind_gust"] = windRainHandler.getWindGustSpeed();
 doc["current_time"] = currentTime(); //send back the last epoch sent in + elapsed time since

 //send wind data back
 weatherWifi.sendPostMessage("/wind",doc,hubWindPort);

 INFOPRINTLN("Posted wind data...");
}

void postRain() 
{
 //rain
 DynamicJsonDocument doc(512);
 doc["rain_rate"] = windRainHandler.getRainRate();
 doc["moisture"] = adcHandler.getMoisture();
 doc["current_time"] = currentTime();

 //send rain data back
 weatherWifi.sendPostMessage("/rain",doc,hubRainPort);

 INFOPRINTLN("Posted rain data...");
}

//refresh weather data
void postWeather() 
{
  DynamicJsonDocument doc(512); 

  doc=refreshBMEDoc(doc);
  doc=refreshADCDoc(doc);
  doc=refreshPMSDoc(doc);
  
  //send data
  weatherWifi.sendPostMessage("/weather",doc,hubWeatherPort);
  
  INFOPRINTLN("Posted general weather data...");
}

//refresh bme data
DynamicJsonDocument refreshBMEDoc(DynamicJsonDocument doc) 
{  
  doc["temperature"] = bmeHandler.getTemperature();
  doc["humidity"] = bmeHandler.getHumidity();
  doc["pressure"] = bmeHandler.getPressure();
  doc["dew_point"] = bmeHandler.getDewPoint();
  doc["heat_index"] = bmeHandler.getHeatIndex();
  doc["current_time"] = currentTime(); //send back the last epoch sent in + elapsed time since

  return doc;
}

//refresh adc data
DynamicJsonDocument refreshADCDoc(DynamicJsonDocument doc) 
{  
   doc["voltage"] = adcHandler.getVoltage();
   doc["ldr"] = adcHandler.getIllumination();
   doc["moisture"] = adcHandler.getMoisture();
   doc["uv"] = adcHandler.getUV();
   doc["current_time"] = currentTime(); //send back the last epoch sent in + elapsed time since
  
   return doc;
}

//refresh air quality data
DynamicJsonDocument refreshPMSDoc(DynamicJsonDocument doc) 
{
 //pms 5003
 doc["pm10"] = pmsHandler.getPM10Standard();
 doc["pm25"] = pmsHandler.getPM25Standard();
 doc["pm100"] = pmsHandler.getPM100Standard();
 doc["current_time"] = currentTime(); //send back the last epoch sent in + elapsed time since
 
 return doc;
}

//The hub driver will POST ip, port, and epoch when handshaking
void syncWithHub() 
{
  DynamicJsonDocument doc=weatherWifi.readContent();

  if (weatherWifi.isPost()) 
  {
      String ip = doc["hubAddress"].as<String>();
      ip.toCharArray(hubAddress, ip.length()+1);
      epoch = doc["epoch"].as<long>();
      millisAtEpoch=millis();

      String deviceName = doc["deviceName"];
      if(deviceName.equals("wind"))
        hubWindPort = doc["hubPort"].as<int>();
      if(deviceName.equals("weather"))
        hubWeatherPort = doc["hubPort"].as<int>();
      if(deviceName.equals("rain"))
        hubRainPort = doc["hubPort"].as<int>();

      INFOPRINT("Hub info: ");
      INFOPRINT("Device: " + deviceName + "  IP: " + ip + ":" + doc["hubPort"].as<int>());
      INFOPRINT("  Epoch: ");
      INFOPRINTLN(epoch);
    
      // Create the response
      // To get the status of the result you can get the http status so
      // this part can be unusefully
      DynamicJsonDocument doc(50);
      doc["status"] = "OK";   
      weatherWifi.sendResponse(doc);

      //Let's see if we still need a handshake
      if(hubWindPort>0 && hubWeatherPort>0 && hubRainPort>0)
      {
        handshakeRequired = false;
        weatherWifi.disableWifi();
      }
  }
}

void setup(void) 
{ 
  #if defined(ERRORDEF) || defined(INFODEF) || defined(VERBOSEDEF)
  Serial.begin(115200);
  #endif

  INFOPRINTLN("--------------------------");
  INFOPRINTLN("Starting up");

  //setup LED pin
  pinMode(LED_BUILTIN, OUTPUT);

  if(firstBoot)
  {
    INFOPRINTLN("Setting up ULP so we can count pulses and hold pins while in deep sleep...  (only does this on first boot)");
    ulp.setupULP();
    ulp.setupWindPin();
    ulp.setupRainPin();
    ulp.setupAirPin();

    //start wifi and http server
    weatherWifi.startWifi();
    weatherWifi.startServer();    

    firstBoot=false;
    airWarmedUp=false;
  }
 
  //free heap
  float heap=(((float)376360-ESP.getFreeHeap())/(float)376360)*100;
  INFOPRINT("Free Heap: (376360 is an empty sketch) ");
  INFOPRINT((int)ESP.getFreeHeap());
  INFOPRINT(" -  ");
  INFOPRINT(heap);
  INFOPRINTLN("% used");

  // Activate mDNS this is used to be able to connect to the server
  // with local DNS hostmane esp8266.local
  if (MDNS.begin("esp8266")) {
    INFOPRINTLN("MDNS responder started");
  }
  else
  {
    ERRORPRINTLN("ERROR: Unable to start MDNS responder");
    
  }

  //make sure air quality sensor is off
  ulp.setAirPinHigh(false);

  INFOPRINTLN("Ready to go!");
  INFOPRINTLN("");
}

//Requesting a new epoch when I wake up
void getEpoch()
{
  DynamicJsonDocument doc = weatherWifi.sendGetMessage("/epoch",hubWeatherPort);
  epoch = doc["epoch"].as<long>();
  millisAtEpoch = millis();

  INFOPRINT("Setting Epoch to ");
  INFOPRINTLN(epoch);
}
 
void loop(void) 
{
  //Read Sensors if we don't need handshake
  readSensors();

  //POST sensor data
  if(currentTime() >= timeToPost)
  {
    if(!weatherWifi.isConnected())
      weatherWifi.startWifi();

    if(hubWeatherPort>0)
    {
      getEpoch();
      postWeather();
    }
    if(hubWindPort>0)
      postWind();
    if(hubRainPort>0)
      postRain();

    //only clear things out if handshake no longer needed
    if(!handshakeRequired)
    {
      timeToPost=currentTime()+WIFITIME;
      weatherWifi.disableWifi();
    }
  }

  //Check in w/ web server if we're in handshake mode
  if(handshakeRequired)
  {
    if(!weatherWifi.isConnected())
    {
      weatherWifi.startWifi();
      weatherWifi.startServer();    
    }
    weatherWifi.listen(10000);
  }
  else
  {
    sleep();      
  } 
}

void readSensors()
{ 
  if(currentTime()>timeToReadSensors)
  {
    INFOPRINTLN("Reading BME and ADC sensors");  
    bmeHandler.init();
    adcHandler.init();
    delay(200);
    
    bmeHandler.storeSamples();
    adcHandler.storeSamples();
    timeToReadSensors=currentTime()+SENSORTIME;
  }

  if(currentTime()>timeToReadWind)
  {
    INFOPRINTLN("Reading wind data");    
    windRainHandler.storeSamples(WINDTIME);
    timeToReadWind=currentTime()+WINDTIME;
  }

  if(currentTime()>timeToReadAir && !airWarmedUp)
  {
    INFOPRINTLN("Warming up air sensor");    
    ulp.setAirPinHigh(true);
    airWarmedUp=true;
    timeToReadAir=currentTime()+TIMEDEEPSLEEP-10;  //warm up for at least a deep sleep cycle w/ a little buffer
  }  

  if(currentTime()>timeToReadAir && airWarmedUp)
  {
    INFOPRINTLN("Reading air now that it's warmed up.");    
    pmsHandler.init();
    if(pmsHandler.storeSamples())
    {
      //shut things down if we got a sample
      ulp.setAirPinHigh(false);
      airWarmedUp=false;
      timeToReadAir=currentTime()+AIRTIME;
    }
  }
}

void sleep(void) 
{
  INFOPRINTLN("Going to ESP deep sleep for " + String(TIMEDEEPSLEEP) + " seconds...  (night night)");
  
  esp_sleep_enable_timer_wakeup(TIMEDEEPSLEEP * TIMEFACTOR);

  esp_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);

  epoch=epoch+TIMEDEEPSLEEP; //add on how much we plan on sleeping now
  esp_deep_sleep_start(); 
}

int currentTime()
{
  return epoch+((millis()-millisAtEpoch)/1000);
}

void blinkLED(int times,int onDuration,int offDuration)
{
  for(int i=0;i<times;i++)
  {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(onDuration);
    digitalWrite(LED_BUILTIN, LOW);
    delay(offDuration);
    
  }
}
