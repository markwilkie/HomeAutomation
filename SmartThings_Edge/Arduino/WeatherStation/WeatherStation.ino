#include "Debug.h"
#include "logger.h"
#include "version.h"
#include "ULP.h"
#include "WeatherWifi.h"
#include "WeatherStation.h"
#include "WindRainHandler.h"
#include "BME280Handler.h"
#include "ADCHandler.h"
#include "PMS5003Handler.h"

#include "Arduino.h"
#include <Preferences.h>
#include <ArduinoJson.h>

//Globals
Preferences preferences;
WeatherWifi weatherWifi;
long millisAtEpoch = 0;
bool wifiOnly = false;   //true so esp never sleeps.  Mostly used for OTA

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
RTC_DATA_ATTR int hubAdminPort=0;
RTC_DATA_ATTR long epoch=0;  //Epoch from hub
RTC_DATA_ATTR long timeToPost = 0;  //seconds for when to http POST

//when to read sensors
RTC_DATA_ATTR long timeToReadSensors=0;   //default
RTC_DATA_ATTR long timeToReadAir=0;       //air quality sensor which we'll rarely read
RTC_DATA_ATTR bool airWarmedUp=false;     //air quality sensor will warm up for a full sleep cycle

//Handlers
RTC_DATA_ATTR WindRainHandler windRainHandler;
RTC_DATA_ATTR ADCHandler adcHandler;
RTC_DATA_ATTR BME280Handler bmeHandler;
RTC_DATA_ATTR PMS5003Handler pmsHandler;

//------------------------------------------------------------

//Get on the same page with the admin driver
void syncSettings()
{
  if(!hubAdminPort)
    return;

  DynamicJsonDocument doc = weatherWifi.sendGetMessage("/settings",hubAdminPort);
  epoch = doc["epoch"].as<long>();
  wifiOnly= doc["wifionly_flag"];
  millisAtEpoch = millis();

  logger.log(INFO,"Setting Epoch to %ld",epoch);

  if(wifiOnly) {
    logger.log(WARNING,"-------------> Wifi is on and ESP will not sleep - ready for OTA"); }
  else {
    logger.log(INFO,"-------------> ESP will sleep now - ready for normal operation"); }

  //Send settings back
  postAdmin();
}

//refresh admin data
void postAdmin() 
{
  if(!hubAdminPort)
    return;

  //admin
  DynamicJsonDocument doc(512);

  if(hubWeatherPort>0)
    doc["hubWeatherPort"] = hubWeatherPort;
  if(hubWindPort>0)
    doc["hubWindPort"] = hubWindPort;
  if(hubRainPort>0)
    doc["hubRainPort"] = hubRainPort; 
  doc["voltage"] = adcHandler.getVoltage();
  doc["wifi_strength"] = weatherWifi.getRSSI();
  doc["firmware_version"] = SKETCH_VERSION;
  doc["health assesment"] = "n/a";
  doc["current_time"] = currentTime(); //send back the last epoch sent in + elapsed time since

  //send admin data back
  if(!weatherWifi.sendPostMessage("/admin",doc,hubAdminPort))
    hubAdminPort=0;

  logger.log(INFO,"Posted admin data...");
}

//refresh wind data
void postWind() 
{
  if(!hubWindPort)
    return;

  //wind
  DynamicJsonDocument doc(512);
  doc["wind_speed"] = windRainHandler.getWindSpeed();
  doc["wind_direction"] = windRainHandler.getDirectionInDeg();
  doc["wind_direction_label"] = windRainHandler.getDirectionLabel();
  doc["wind_gust"] = windRainHandler.getWindGustSpeed();
  doc["current_time"] = currentTime(); //send back the last epoch sent in + elapsed time since

  //send wind data back
  if(!weatherWifi.sendPostMessage("/wind",doc,hubWindPort))
    hubWeatherPort=0;

  logger.log(INFO,"Posted wind data...");
}

void postRain() 
{
  if(!hubRainPort)
    return;

  //rain
  DynamicJsonDocument doc(512);
  doc["rain_rate"] = windRainHandler.getRainRate();
  doc["moisture"] = adcHandler.getMoisture();
  doc["current_time"] = currentTime();

  //send rain data back
  if(!weatherWifi.sendPostMessage("/rain",doc,hubRainPort))
    hubRainPort=0;

  logger.log(INFO,"Posted rain data...");
}

//refresh weather data
void postWeather() 
{
  if(!hubWeatherPort)
    return;

  DynamicJsonDocument doc(512); 

  doc=refreshBMEDoc(doc);
  doc=refreshADCDoc(doc);
  doc=refreshPMSDoc(doc);
  
  //send data
  if(!weatherWifi.sendPostMessage("/weather",doc,hubWeatherPort))
    hubRainPort=0;
  
  logger.log(INFO,"Posted general weather data...");
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
 doc["pm25"] = pmsHandler.getPM25Standard();;
 doc["pm100"] = pmsHandler.getPM100Standard();;
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
      if(deviceName.equals("admin"))
        hubAdminPort = doc["hubPort"].as<int>();
      if(deviceName.equals("wind"))
        hubWindPort = doc["hubPort"].as<int>();
      if(deviceName.equals("weather"))
        hubWeatherPort = doc["hubPort"].as<int>();
      if(deviceName.equals("rain"))
        hubRainPort = doc["hubPort"].as<int>();

      logger.log(VERBOSE,"Hub info - Device: %s, IP: %S, Port: %d, Epoch: %ld",deviceName,ip,doc["hubPort"].as<int>(),epoch);
    
      // Create the response
      // To get the status of the result you can get the http status so
      // this part can be unusefully
      DynamicJsonDocument doc(50);
      doc["status"] = "OK";   
      weatherWifi.sendResponse(doc);

      //Let's see if we still need a handshake 
      if(hubWindPort>0 && hubWeatherPort>0 && hubRainPort>0 && hubAdminPort>0)
        handshakeRequired = false;
  }
}

void setup(void) 
{ 
  #if defined(ERRORDEF) || defined(INFODEF) || defined(VERBOSEDEF)
  Serial.begin(115200);
  #endif

  logger.log(INFO,"--------------------------");

  //setup LED pin
  pinMode(LED_BUILTIN, OUTPUT);

  if(firstBoot)
    initialSetup();   

  //read wind and rain sensor  (it's hear because we must be sure to read (clear) the register during windy conditions so there's no overflow
  //   it is known that on initial boot, there's no epoch which makes the canculations for wind be invalid.  a full  cycle must be waited for and it'll clear itself
  logger.log(INFO,"Reading wind and rain sensors");
  windRainHandler.storeSamples();
}

void initialSetup()
{
    //blink led (1x2000,3x250)
    blinkLED(1,2000,500);
    blinkLED(3,250,250);

    //free heap
    float heap=(((float)376360-ESP.getFreeHeap())/(float)376360)*100;
    logger.log(INFO,"Free Heap: %d or %f% used",(int)ESP.getFreeHeap(),heap);

    //Setting up flash 
    //preferences.begin("weather", false); 
      
    logger.log(INFO,"Setting up ULP so we can count pulses and hold pins while in deep sleep...  (only does this on first boot)");
    ulp.setupULP();
    ulp.setupWindPin();
    ulp.setupRainPin();
    ulp.setupAirPin();

    //start wifi and http server
    weatherWifi.startWifi();
    weatherWifi.startServer();   

    firstBoot=false;
    airWarmedUp=false;

    //make sure air quality sensor is off
    ulp.setAirPinHigh(false);
}

void loop(void) 
{
  //Check in w/ web server if we're in handshake mode
  if(handshakeRequired || wifiOnly)
  {
    //Blink because we're handshaking (2x500)
    blinkLED(2,500,250);

    //start wifi if not already on
    if(!weatherWifi.isConnected())
      weatherWifi.startWifi();

    //start server and listen on it if not already started
    if(!weatherWifi.isServerOn())
      weatherWifi.startServer();

    //listen for a bit
    weatherWifi.listen(HTTPSERVERTIME*1000);

    //Since we're not deep sleeping (e.g. booting), be sure and read the wind/rain sensor when we read the others.  Otherwise, we'll do this on each boot.
    if(currentTime()>=timeToReadSensors)
    {
      logger.log(INFO,"Reading wind and rain sensors (in loop)");
      windRainHandler.storeSamples();    
    }
  }

  //Read Sensors if it's time to  (each member function checks its own time)
  readSensors();
  readAirSensor();  //this one is seperate because we don't read it very often as it takes a lot of power

  //POST sensor data if it's time to
  if(currentTime() >= timeToPost)
    postSensorData();  

  //only shut down wifi and deep sleep out if handshake no longer needed and we're not in OTA mode
  if(!handshakeRequired && !wifiOnly)
  {
    timeToPost=currentTime()+WIFITIME-(WIFITIME*.1);  //10% buffer
    weatherWifi.disableWifi();
    sleep();   //deep sleep
  }    
}

void postSensorData()
{
  //Fire up wifi if not already connected
  if(!weatherWifi.isConnected())
    weatherWifi.startWifi();

  //if we've got a port number from handshaking, go ahead and post
  syncSettings();
  postWeather();
  postWind();
  postRain();  
}

void readSensors()
{ 
  if(currentTime()<timeToReadSensors)
    return;

  logger.log(INFO,"Reading BME and ADC sensors");  
  bmeHandler.init();
  adcHandler.init();
  delay(200);
  
  bmeHandler.storeSamples();
  adcHandler.storeSamples();
  timeToReadSensors=currentTime()+SENSORTIME-(SENSORTIME*.1);
}

void readAirSensor()
{
  //Check time 
  if(currentTime()<timeToReadAir)
    return;

  //Check if we've got enough voltage.  At night, there's usually less than 5v and readings are wonky
  if(adcHandler.getVoltage()<PMSMINVOLTAGE)
  {
    logger.log(WARNING,"Voltage too low to take a PMS5003 reading (%f volts)",adcHandler.getVoltage());
    return;
  }

  if(!airWarmedUp)
  {
    logger.log(INFO,"Warming up air sensor");    
    ulp.setAirPinHigh(true);
    airWarmedUp=true;
    timeToReadAir=currentTime()+TIMEDEEPSLEEP-(TIMEDEEPSLEEP*.1);  //warm up for at least a deep sleep cycle w/ a little buffer
    return;
  }  

  if(airWarmedUp)
  {
    logger.log(INFO,"Reading air now that it's warmed up.");    
    pmsHandler.init();
    pmsHandler.storeSamples();
    
    //shut things down even if we didn't get a sample
    ulp.setAirPinHigh(false);
    airWarmedUp=false;
    timeToReadAir=currentTime()+AIRTIME-(AIRTIME*.1);
    return;
  }
}

void sleep(void) 
{
  logger.log(INFO,"Going to ESP deep sleep for %d seconds.  (night night)",TIMEDEEPSLEEP);
  
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
