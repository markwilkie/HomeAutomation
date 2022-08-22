#include "WindRain.h"
#include "WeatherStation.h"
#include "Debug.h"

//Time since the last reading is passed in
float WindRain::getWindSpeed(long timeSinceLastReading)
{ 
  int windPulsesRaw = ulp.getULPWindPulseCount();
  VERBOSEPRINT("Wind Pulses: ");
  VERBOSEPRINT(windPulsesRaw);
  VERBOSEPRINT("  Time Elapsed: ");
  VERBOSEPRINTLN(timeSinceLastReading);

  float wind = (((float)windPulsesRaw / (float)timeSinceLastReading) * WINDFACTOR) / 20.0;  //there's 20 pulses per revolution 

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

  return windGust;
}

float WindRain::getRainRate()
{
  int rainPulsesRaw = ulp.getULPRainPulseCount();
  VERBOSEPRINT("Rain Pulses: ");
  VERBOSEPRINTLN(rainPulsesRaw);

  float inchesRain = (float)rainPulsesRaw*RAINFACTOR;

  return inchesRain;
}
