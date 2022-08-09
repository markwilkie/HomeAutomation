#include <Arduino.h>

#include "WindRain.h"

#define TIMEDEEPSLEEP         10          // amount in seconds the ESP32 sleeps

WindRain windRain;

void setup() 
{
  Serial.begin(115200); 
  setCpuFrequencyMhz(80);

  dump();  
  sleep();  //deep esp sleep    
}

void loop() 
{
  Serial.println("waiting 10 seconds...");
  delay(10000);
  dump();
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
