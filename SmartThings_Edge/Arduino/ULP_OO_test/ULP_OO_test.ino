#include <Arduino.h>

#include "esp32/ulp.h"
#include "ulptool.h"
#include "ULP.h"

#define TIMEDEEPSLEEP         10          // amount in seconds the ESP32 sleeps
#define TIMEFACTOR        1000000     // factor between seconds and microseconds

#define WINDFACTOR        3.4         // 3.4 km/h per revolution, or 20 pulses

RTC_DATA_ATTR ULP ulp;
RTC_DATA_ATTR int bootCount=0;

void setup() 
{
  Serial.begin(115200); 
  setCpuFrequencyMhz(80);
  ++bootCount;

  Serial.print("Hi! \nCurrent boot count: ");
  Serial.println(bootCount); 

  //only setup once
  if(bootCount==1)
  {
    ulp.setupULP();
  }
  getPulses();  
  sleep();  //deep esp sleep    
}

void loop() 
{
  Serial.println("waiting 10 seconds...");
  delay(10000);
  getPulses();
}

void sleep(void) 
{
  Serial.println("Set timer for ESP deep sleep-wakeup every " + String(TIMEDEEPSLEEP) + " seconds.");
  
  esp_sleep_enable_timer_wakeup(TIMEDEEPSLEEP * TIMEFACTOR);

  esp_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);

  esp_deep_sleep_start(); 
}

void getPulses() 
{
  float wind = 0;
  bool windSuccess = false;
  float windGust = 0;
  bool windGustSuccess = false;
   
    // Wind Speed and Rain
    int rainPulsesRaw = ulp.getULPRainPulseCount();
    int windPulsesRaw = ulp.getULPWindPulseCount();
    int shortestWindPulse = ulp.getULPShortestWindPulse();

    Serial.print("Rain Pulses: ");
    Serial.print(rainPulsesRaw);
    Serial.print("  Wind Pulses: ");
    Serial.print(windPulsesRaw);
    Serial.println(" pulses.");

    Serial.print("Shortest wind pulse: ");
    Serial.print(shortestWindPulse);    
    Serial.print("  Ticks: ");
    Serial.println((ulp_wind_tick_cnt & UINT16_MAX));   

    //Calc wind
    wind = (((float)windPulsesRaw / (float)TIMEDEEPSLEEP) * WINDFACTOR) / 20;  //there's 20 pulses per revolution 
    windGust = ((1 / ((float)shortestWindPulse / (float)TIMEFACTOR)) * WINDFACTOR)/20;       
    Serial.print("Wind (kts): ");
    Serial.print(wind);
    Serial.print("    Gust (kts): ");
    Serial.println(windGust);          
}
