#include <Arduino.h>
#include <WiFi.h>

#include "PapertrailLogger.h"
#include "TestHandler.h"
#include "WindRain.h"

#define TIMEDEEPSLEEP         10          // amount in seconds the ESP32 sleeps


#define SSID "WILKIE-LFP"
#define PASSWORD "4777ne178"

#define PAPERTRAIL_HOST "logs4.papertrailapp.com"
#define PAPERTRAIL_PORT 54449

RTC_DATA_ATTR WindRain windRain;
//RTC_DATA_ATTR TestHandler testHandler;

PapertrailLogger *errorLog;
PapertrailLogger *warningLog;
PapertrailLogger *noticeLog;
PapertrailLogger *debugLog;
PapertrailLogger *infoLog; 

void setup() 
{
  Serial.begin(115200); 
  setCpuFrequencyMhz(80);

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println("WiFiConnection Failed");
    delay(5000);
    ESP.restart();
  }  

  Serial.println("Woke up");
  errorLog = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Error, "\033[0;31m", "weatherstation", " ");
  warningLog = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Warning, "\033[0;33m", "weatherstation", " ");
  noticeLog = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Notice, "\033[0;36m", "weatherstation", " ");
  debugLog= new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Debug, "\033[0;32m","weatherstation", " ");
  infoLog = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Info, "\033[0;34m", "weatherstation", " ");

  Serial.println(windRain.getWindSpeed(TIMEDEEPSLEEP)); 
  //dump();  
}

void loop() 
{
  Serial.println("waiting1");
  delay(2000);
  noticeLog->printf("wind1 %f\n", windRain.getWindSpeed(TIMEDEEPSLEEP));
  Serial.println(windRain.getWindSpeed(TIMEDEEPSLEEP)); 
  Serial.println("waiting2");
  delay(2000);
  noticeLog->printf("wind2 %f\n", windRain.getWindSpeed(TIMEDEEPSLEEP));
  Serial.println(windRain.getWindSpeed(TIMEDEEPSLEEP)); 
  
  //dump();  
  //debugLog-->printf("going back to sleep\n");
  Serial.println("going back to sleep");   
  sleep();  //deep esp sleep    
}

void sleep(void) 
{
  Serial.println("Set timer for ESP deep sleep-wakeup every " + String(TIMEDEEPSLEEP) + " seconds.");
  
  esp_sleep_enable_timer_wakeup(TIMEDEEPSLEEP * TIMEFACTOR);

  esp_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);

  esp_deep_sleep_start(); 
}

void dump() 
{   
    Serial.println("====================================");
    Serial.println("====================================");
    Serial.print("Wind (kts): ");
    Serial.print(windRain.getWindSpeed(TIMEDEEPSLEEP));
    Serial.print("    Gust (kts): ");
    Serial.println(windRain.getWindGustSpeed());     

    Serial.print("Rain (inches): ");
    Serial.println(windRain.getRainRate());         
    Serial.println();
}
