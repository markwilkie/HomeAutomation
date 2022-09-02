#include "WindRain.h"

//Time since the last reading is passed in
float WindRain::getWindSpeed(long timeSinceLastReading)
{ 
  int windPulsesRaw = ulp.getULPWindPulseCount();
  float wind = (((float)windPulsesRaw / (float)timeSinceLastReading) * WINDFACTOR) / 20.0;  //there's 20 pulses per revolution 
  logger.log(VERBOSE,"Wind Pulses: %d, Time Elpased: %d, Calc'd speed: %f",windPulsesRaw,timeSinceLastReading,wind);

  return wind;
}

float WindRain::getWindGustSpeed()
{
  uint32_t shortestWindPulseTime = ulp.getULPShortestWindPulseTime();
  if(shortestWindPulseTime==0)
    return 0;

  //figure gust
  float pulsesPerSec = TIMEFACTOR/shortestWindPulseTime;  
  float windGust = ((float)pulsesPerSec * WINDFACTOR) / 20; 

  logger.log(VERBOSE,"Shortest wind pulse Time (ms): %d, calc'd speed: %f",(int)shortestWindPulseTime,windGust);

  return windGust;
}

float WindRain::getRainRate()
{
  int rainPulsesRaw = ulp.getULPRainPulseCount();
  float inchesRain = (float)rainPulsesRaw*RAINFACTOR;

  logger.log(VERBOSE,"Rain Pulses: %d, calc'd inches: %f",rainPulsesRaw,inchesRain);

  return inchesRain;
}
