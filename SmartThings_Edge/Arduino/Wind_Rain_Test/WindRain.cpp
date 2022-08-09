#include "WindRain.h"

RTC_DATA_ATTR ULP ulp;
RTC_DATA_ATTR int bootCount = 0;

WindRain::WindRain()
{
  ++bootCount;

  Serial.print("Hi! \nCurrent boot count: ");
  Serial.println(bootCount); 

  //only setup once
  if(bootCount==1)
  {
    Serial.println("Setting up ULP");
    ulp.setupULP();
  }  
}

float WindRain::getWindSpeed(int timeDeepSleep)
{
  int windPulsesRaw = ulp.getULPWindPulseCount();
  Serial.print("  Wind Pulses: ");
  Serial.print(windPulsesRaw);

  float wind = (((float)windPulsesRaw / (float)timeDeepSleep) * WINDFACTOR) / 20;  //there's 20 pulses per revolution 
  Serial.print("   Wind (kts): ");
  Serial.println(wind);

  return wind;
}

float WindRain::getWindGustSpeed(int timeFactor)
{
  uint32_t shortestWindPulseTime = ulp.getULPShortestWindPulseTime();
  Serial.print("Shortest wind pulse Time (ms): ");
  Serial.print(shortestWindPulseTime);

  if(shortestWindPulseTime==0)
    return 0;

  float pulsesPerSec = timeFactor/shortestWindPulseTime;  
  Serial.print("  Pulses /s: ");
  Serial.print(pulsesPerSec);
  
  float windGust = ((float)pulsesPerSec * WINDFACTOR) / 20; 
  Serial.print("   Gust (kts): ");
  Serial.println(windGust);   

  return windGust;
}

float WindRain::getRainRate()
{
  int rainPulsesRaw = ulp.getULPRainPulseCount();
  Serial.print("Rain Pulses: ");
  Serial.print(rainPulsesRaw);

  float inchesRain = (float)rainPulsesRaw*RAINFACTOR;
  Serial.print("   Rain (inches): ");
  Serial.println(inchesRain); 

  return inchesRain;
}
