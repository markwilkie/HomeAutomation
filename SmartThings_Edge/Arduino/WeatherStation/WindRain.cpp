#include "WindRain.h"
#include "WeatherStation.h"
#include "Debug.h"

RTC_DATA_ATTR ULP ulp;

WindRain::WindRain()
{
  //only setup once
  if(bootCount==1)
  {
    INFOPRINTLN("Setting up ULP for pulse counting...  (only does this on first boot)");
    ulp.setupULP();
  }  
}

float WindRain::getWindSpeed(int timeDeepSleep)
{
  int windPulsesRaw = ulp.getULPWindPulseCount();
  VERBOSEPRINT("Wind Pulses: ");
  VERBOSEPRINT(windPulsesRaw);

  float wind = (((float)windPulsesRaw / (float)timeDeepSleep) * WINDFACTOR) / 20;  //there's 20 pulses per revolution 
  VERBOSEPRINT("   Wind (kts): ");
  VERBOSEPRINTLN(wind);

  return wind;
}

float WindRain::getWindGustSpeed()
{
  uint32_t shortestWindPulseTime = ulp.getULPShortestWindPulseTime();
  VERBOSEPRINT("Shortest wind pulse Time (ms): ");
  VERBOSEPRINTLN(shortestWindPulseTime);

  if(shortestWindPulseTime==0)
    return 0;

  float pulsesPerSec = TIMEFACTOR/shortestWindPulseTime;  
  float windGust = ((float)pulsesPerSec * WINDFACTOR) / 20; 
  VERBOSEPRINT("Gust (kts): ");
  VERBOSEPRINTLN(windGust);   

  return windGust;
}

float WindRain::getRainRate()
{
  int rainPulsesRaw = ulp.getULPRainPulseCount();
  VERBOSEPRINT("Rain Pulses: ");
  VERBOSEPRINT(rainPulsesRaw);

  float inchesRain = (float)rainPulsesRaw*RAINFACTOR;
  VERBOSEPRINT("   Rain (inches): ");
  VERBOSEPRINTLN(inchesRain); 

  return inchesRain;
}
