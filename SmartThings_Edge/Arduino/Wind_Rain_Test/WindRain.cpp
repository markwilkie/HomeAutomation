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
  int shortestWindPulse = ulp.getULPShortestWindPulse();
  Serial.print("Shortest wind pulse: ");
  Serial.print(shortestWindPulse);
    
  float windGust = ((1 / ((float)shortestWindPulse / (float)timeFactor)) * WINDFACTOR)/20; 
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
