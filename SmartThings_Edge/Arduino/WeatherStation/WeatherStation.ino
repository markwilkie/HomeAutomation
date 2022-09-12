#include "Arduino.h"
#include <rom/rtc.h>
#include <Preferences.h>
#include <ArduinoJson.h>

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

//Globals
Preferences preferences;
WeatherWifi weatherWifi;
unsigned long millisAtEpoch = 0;
bool wifiOnly = false;        //true so esp never sleeps.  Mostly used for OTA   

//globals in ULP which survive deep sleep
RTC_DATA_ATTR bool firstBoot = true;
RTC_DATA_ATTR bool powerSaverMode = false;
RTC_DATA_ATTR bool boostMode = true;  //uses boosted capacitor as power
RTC_DATA_ATTR ULP ulp;

//when true, ESP goes into a high battery drain state of waiting for a /handshake POST message
RTC_DATA_ATTR bool handshakeRequired = true;

//info from hub obtained by handshaking
RTC_DATA_ATTR char hubAddress[30]="";
RTC_DATA_ATTR int hubWindPort=0;
RTC_DATA_ATTR int hubWeatherPort=0;
RTC_DATA_ATTR int hubRainPort=0;
RTC_DATA_ATTR int hubAdminPort=0;
RTC_DATA_ATTR unsigned long epoch=0;  //Epoch from hub

//when to read air sensor
RTC_DATA_ATTR unsigned long timeToReadAir=0;       //air quality sensor which we'll rarely read
RTC_DATA_ATTR bool airWarmingUp=false;             //air quality sensor will warm up for a full sleep cycle

//Handlers
RTC_DATA_ATTR WindRainHandler windRainHandler;
RTC_DATA_ATTR ADCHandler adcHandler;
RTC_DATA_ATTR BME280Handler bmeHandler;
RTC_DATA_ATTR PMS5003Handler pmsHandler;

//------------------------------------------------------------

//Send a GET message to the admin driver to get epoch, wifi-only flag and so on.
void syncSettings()
{
  if(!hubAdminPort)
    return;

  DynamicJsonDocument doc = weatherWifi.sendGetMessage("/settings",hubAdminPort);
  epoch = doc["epoch"].as<unsigned long>();
  wifiOnly= doc["wifionly_flag"];
  millisAtEpoch = millis();

  logger.log(INFO,"Setting Epoch to %s (%ld)",getTimeString(epoch),epoch);

  if(wifiOnly) {
    logger.log(WARNING,"Wifi is on and ESP will not sleep - ready for OTA updates"); }
  else {
    logger.log(INFO,"ESP is able to sleep now - ready for normal operation"); }

  if(handshakeRequired)
    logger.log(WARNING,"Handshake mode active");
  else
    logger.log(INFO,"Handshake mode NOT active");

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
  doc["cap_voltage"] = adcHandler.getCapVoltage();
  doc["vcc_voltage"] = adcHandler.getVCCVoltage();
  doc["wifi_strength"] = weatherWifi.getRSSI();
  doc["firmware_version"] = SKETCH_VERSION;
  doc["heap_frag"] = (1.0-((float)ESP.getMinFreeHeap()/(float)ESP.getFreeHeap()))*100;
  doc["pms_read_time"] = pmsHandler.getLastReadTime();
  doc["power_saver_mode"] = powerSaverMode;   
  doc["wifi_only"] = wifiOnly;
  doc["boost_mode"] = boostMode;
  doc["cpu_reset_code"] = rtc_get_reset_reason(0);
  doc["cpu_reset_reason"] = getResetReason(0);
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
   doc["cap_voltage"] = adcHandler.getCapVoltage();
   doc["vcc_voltage"] = adcHandler.getVCCVoltage();
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
  doc["pm100"] = pmsHandler.getPM100Standard();
  doc["pm25AQI"] = pmsHandler.getPM25AQI(); 
  doc["pm25Label"] = pmsHandler.getPM25Label(); 
  doc["pm100AQI"] = pmsHandler.getPM100AQI(); 
  doc["pm100Label"] = pmsHandler.getPM100Label(); 
  doc["pms_read_time"] = pmsHandler.getLastReadTime(); //send back the last epoch sent in + elapsed time since
 
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
      epoch = doc["epoch"].as<unsigned long>();
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

      logger.log(VERBOSE,"Hub info - Device: %s, IP: %s, Port: %d, Epoch: %ld  (%s)",deviceName,ip.c_str(),doc["hubPort"].as<int>(),epoch,getTimeString(epoch));
    
      // Create the response
      // To get the status of the result you can get the http status so
      // this part can be unusefully
      DynamicJsonDocument doc(50);
      doc["status"] = "OK";   
      weatherWifi.sendResponse(doc);

      //Let's see if we still need a handshake 
      if(hubWindPort>0 && hubWeatherPort>0 && hubRainPort>0 && hubAdminPort>0)
      {
        //Save address and ports to flash in case we reboot
        putHubInfoIntoPreference();
        handshakeRequired = false;
      }
  }
}

void setup(void) 
{ 
  #if defined(ERRORDEF) || defined(INFODEF) || defined(VERBOSEDEF)
  Serial.begin(115200);
  #endif

  //setup LED pin
  pinMode(LED_BUILTIN, OUTPUT);

  if(firstBoot)
  {
    logger.log(INFO,"-------------------------- Booting");
    logger.log(INFO,"CPU 0 Reset Code: %d Reason: %s",rtc_get_reset_reason(0),getResetReason(0));

    //Check if wifi boot
    if(getWifiBootFlag())
    {
      logger.log(ERROR,"ESP rebooted because it couldn't get a wifi connection"); 
    }    

    //Important setup stuff
    initialSetup();
  }
  else
  {
    logger.log(INFO,"-------------------------- Waking up");
  }

  //read wind and rain sensor  (it's here because we must be sure to read (clear) the register during windy conditions so there's no overflow
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
    logger.log(INFO,"Free Heap: %d or %f%% used",(int)ESP.getFreeHeap(),heap);

    //Setting up flash 
    //preferences.begin("weather", false); 
      
    logger.log(WARNING,"First Boot.  Now setting up ULP so we can count pulses and hold pins while in deep sleep...");
    ulp.setupULP();
    ulp.setupWindPin();
    ulp.setupRainPin();
    ulp.setupAirPin();
    ulp.setupBoostPin();

    //Try and load address and ports from flash
    getHubInfoFromPreference();

    //Now let's see if we still need a handshake 
    if(hubWindPort>0 && hubWeatherPort>0 && hubRainPort>0 && hubAdminPort>0)
    {
      handshakeRequired = false;
    }     

    //start wifi and http server
    weatherWifi.startWifi();
    weatherWifi.startServer();   

    firstBoot=false;
    airWarmingUp=false;

    //make sure air quality sensor is off and boost is on by default
    ulp.setAirPinHigh(false);
    ulp.setBoostPinHigh(true);
}

void loop(void) 
{
  //Read most sensors if it's time to  (each member function checks its own time)
  readSensors();
  
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
    logger.log(INFO,"Reading wind and rain sensors (in loop)");
    windRainHandler.storeSamples();    
  }

  //check if we need to be in power saving mode
  checkPowerSavingMode();

  //this one is seperate because we don't read it very often as it takes a lot of power
  readAirSensor();  

  //POST sensor data
  postSensorData();  

  //only shut down wifi and deep sleep out if handshake no longer needed and we're not in OTA mode
  if((!handshakeRequired && !wifiOnly) || (powerSaverMode && !wifiOnly))
  {
    long sleepSeconds=getSleepSeconds();
    logger.log(INFO,"Going to ESP deep sleep for %d seconds, will wake up at %s.  (night night)",sleepSeconds,getTimeString(currentTime()+sleepSeconds));
    weatherWifi.disableWifi();
    sleep(sleepSeconds);   //deep sleep
  }    
}

void checkPowerSavingMode()
{
  //Get capcitor voltage
  float voltage=adcHandler.getCapVoltage(); 
  //Check if we should turn booster off and switch to battery power
  if(voltage<=SWITCHTOTOBAT && boostMode) {
    ulp.setBoostPinHigh(false);
    boostMode=false;
    logger.log(WARNING,"Switching to battery power");
  }
  if(voltage>SWITCHTOTOCAP && !boostMode) {
    ulp.setBoostPinHigh(true);
    boostMode=true;
    logger.log(WARNING,"Switching to capacitor power");
  }

  //Check if we should be in power saver mode by checking voltage at vcc pin
  voltage=adcHandler.getVCCVoltage(); 
  if(voltage<=POWERSAVERVOLTAGE && voltage>0 && powerSaverMode) {
    logger.log(WARNING,"*** Still Power Saver Mode.. (%fV) ***",voltage);
  }
  if(voltage>POWERSAVERVOLTAGE && powerSaverMode) {
    powerSaverMode=false;
    logger.log(WARNING,"*** Exiting Power Saver Mode. (%fV) ***",voltage);
  }
  if(voltage<=POWERSAVERVOLTAGE && voltage>0 && !powerSaverMode) {
    powerSaverMode=true;
    logger.log(WARNING,"*** Entering Power Saver Mode. (%fV) ***",voltage);
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
  logger.log(INFO,"Reading BME and ADC sensors");  
  bmeHandler.init();
  adcHandler.init();
  delay(200);
  
  bmeHandler.storeSamples();
  adcHandler.storeSamples();
}

void readAirSensor()
{
  //Check time and if we're in power saver mode
  if(currentTime()<timeToReadAir)
    return;

  //Check if we've got enough voltage.  At night, there's usually less than 5v and readings are wonky
  if(adcHandler.getCapVoltage()<PMSMINVOLTAGE || powerSaverMode)
  {
    ulp.setAirPinHigh(false);  //make sure pin is low
    if(!boostMode)    
      ulp.setBoostPinHigh(false);    
    airWarmingUp=false;
    timeToReadAir=(currentTime()+AIRREADTIME)-10;  //100 is just a buffer
    logger.log(WARNING,"Voltage too low to take a PMS5003 reading - or in power saver mode (%f volts)",adcHandler.getVCCVoltage());
    return;
  }

  if(!airWarmingUp)
  {
    logger.log(INFO,"Warming up air sensor");    
    ulp.setAirPinHigh(true);
    if(!boostMode)    
      ulp.setBoostPinHigh(true);
    airWarmingUp=true;
    timeToReadAir=(currentTime()+AIRWARMUPTIME)-5;  //5 is just a buffer
    return;
  }  

  if(airWarmingUp)
  {
    logger.log(INFO,"Reading air now that it's warmed up.");    
    pmsHandler.init();
    pmsHandler.storeSamples();
    
    //shut things down even if we didn't get a sample
    ulp.setAirPinHigh(false);
    if(!boostMode)
      ulp.setBoostPinHigh(false);    
    airWarmingUp=false;
    timeToReadAir=(currentTime()+AIRREADTIME)-10;  //100 is just a buffer
    logger.log(VERBOSE,"Setting next air sensor wakeup time to: %s",getTimeString(timeToReadAir)); 
    return;
  }
}

//Determine how long to sleep for
long getSleepSeconds()
{
  //see if we're in power saver mode, thus sleeping longer
  if(powerSaverMode)
  {
    return POWERSAVERTIME;
  }

  //Let's see if we need to warm up sensor not
  if(airWarmingUp)
  {
    return AIRWARMUPTIME;
  }

  //Looks like it's a normal cycle time
  return CYCLETIME;
}

//Deep sleep
void sleep(long sleepSeconds) 
{
  epoch=epoch+sleepSeconds; //add on how much we plan on sleeping now
  uint64_t sleepTime = (uint64_t)sleepSeconds*(uint64_t)TIMEFACTOR;  //calc micro-seconds to sleep

  esp_sleep_enable_timer_wakeup(sleepTime); 
  esp_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
  esp_deep_sleep_start(); 
}

unsigned long currentTime()
{
  return epoch+((millis()-millisAtEpoch)/1000);
}

// Print the UTC time from time_t, using C library functions.
char *getTimeString(time_t now) 
{
  static char timeString[13];
  
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  int year = timeinfo.tm_year + 1900; // tm_year starts in year 1900 (!)
  int month = timeinfo.tm_mon + 1; // tm_mon starts at 0 (!)
  int day = timeinfo.tm_mday; // tm_mday starts at 1 though (!)
  int hour = timeinfo.tm_hour;
  int mins = timeinfo.tm_min;
  int sec = timeinfo.tm_sec;
  int day_of_week = timeinfo.tm_wday; // tm_wday starts with Sunday=0

  sprintf(timeString,"%02d:%02d %02d/%02d ",hour, mins, month, day);
  return timeString;
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

//Note ESP bug:  https://github.com/espressif/esp-idf/issues/494 
const char* getResetReason(int icore) 
{ 
  const char* resetReason;
  RESET_REASON reason = rtc_get_reset_reason( (RESET_REASON) icore); 

  switch ( reason)
  {
    case 1 : resetReason="POWERON_RESET";break;          /**<1, Vbat power on reset*/
    case 3 : resetReason="SW_RESET";break;               /**<3, Software reset digital core*/
    case 4 : resetReason="OWDT_RESET";break;             /**<4, Legacy watch dog reset digital core*/
    case 5 : resetReason="DEEPSLEEP_RESET";break;        /**<5, Deep Sleep reset digital core*/
    case 6 : resetReason="SDIO_RESET";break;             /**<6, Reset by SLC module, reset digital core*/
    case 7 : resetReason="TG0WDT_SYS_RESET";break;       /**<7, Timer Group0 Watch dog reset digital core*/
    case 8 : resetReason="TG1WDT_SYS_RESET";break;       /**<8, Timer Group1 Watch dog reset digital core*/
    case 9 : resetReason="RTCWDT_SYS_RESET";break;       /**<9, RTC Watch dog Reset digital core*/
    case 10 : resetReason="INTRUSION_RESET";break;       /**<10, Instrusion tested to reset CPU*/
    case 11 : resetReason="TGWDT_CPU_RESET";break;       /**<11, Time Group reset CPU*/
    case 12 : resetReason="SW_CPU_RESET";break;          /**<12, Software reset CPU*/
    case 13 : resetReason="RTCWDT_CPU_RESET";break;      /**<13, RTC Watch dog Reset CPU*/
    case 14 : resetReason="EXT_CPU_RESET";break;         /**<14, for APP CPU, reseted by PRO CPU*/
    case 15 : resetReason="RTCWDT_BROWN_OUT_RESET";break;/**<15, Reset when the vdd voltage is not stable*/
    case 16 : resetReason="RTCWDT_RTC_RESET";break;      /**<16, RTC Watch dog reset digital core and rtc module*/
    default : resetReason="NO_MEAN";
  }

  return resetReason;
}

//Check if the ESP is booting because it couldn't get a wifi connection
bool getWifiBootFlag()
{
  //See if the flag is set
  preferences.begin("lfpweather", true);  // read only
  bool retVal=preferences.getBool("WIFI_BOOT_FLAG", false); // read only
  preferences.end();

  //If flag is set, reset now
  if(retVal)
  {
    preferences.begin("lfpweather", false);  // read/write
    preferences.putBool("WIFI_BOOT_FLAG", false);
    preferences.end();    
  }
  
  return retVal;  
}

//Set flag that says we couldn't get a wifi connection
void setWifiBootFlag()
{ 
  preferences.begin("lfpweather", false);  // read/write
  preferences.putBool("WIFI_BOOT_FLAG", true);
  preferences.end();
}

//put bool named pair into flash
void putHubInfoIntoPreference()
{
  preferences.begin("lfpweather", false);
  preferences.putBytes("hubAddress", hubAddress,30);
  preferences.putInt("hubWindPort", hubWindPort);
  preferences.putInt("hubWeatherPort", hubWeatherPort);
  preferences.putInt("hubRainPort", hubRainPort);
  preferences.putInt("hubAdminPort", hubAdminPort);  
  preferences.end();
}

//put bool named pair into flash
void getHubInfoFromPreference()
{
  preferences.begin("lfpweather", true);
  preferences.getBytes("hubAddress", hubAddress, 30);
  hubWindPort=preferences.getInt("hubWindPort",0);
  hubWeatherPort=preferences.getInt("hubWeatherPort",0);
  hubRainPort=preferences.getInt("hubRainPort", 0);
  hubAdminPort=preferences.getInt("hubAdminPort", 0);  
  preferences.end();
}
