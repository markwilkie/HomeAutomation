#include "WindRain.h"

//Time since the last reading is passed in
double WindRain::getWindSpeed(long timeSinceLastReading)
{ 
  int windPulsesRaw = ulp.getULPWindPulseCount();
  double wind = (((double)windPulsesRaw / (double)timeSinceLastReading) * WINDFACTOR) / 20.0;  //there's 20 pulses per revolution 
  logger.log(VERBOSE,"Wind Pulses: %d, Time Elpased: %d, Calc'd speed: %f",windPulsesRaw,timeSinceLastReading,wind);

  return wind;
}

double WindRain::getWindGustSpeed()
{
  uint32_t shortestWindPulseTime = ulp.getULPShortestWindPulseTime();
  if(shortestWindPulseTime==0)
    return 0;

  //figure gust
  double pulsesPerSec = TIMEFACTOR/shortestWindPulseTime;  
  double windGust = ((double)pulsesPerSec * WINDFACTOR) / 20; 

  logger.log(VERBOSE,"Shortest wind pulse Time (ms): %d, calc'd speed: %f",(int)shortestWindPulseTime,windGust);

  return windGust;
}

double WindRain::getRainRate()
{
  int rainPulsesRaw = ulp.getULPRainPulseCount();
  double inchesRain = (double)rainPulsesRaw*RAINFACTOR;

  logger.log(VERBOSE,"Rain Pulses: %d, calc'd inches: %f",rainPulsesRaw,inchesRain);

  return inchesRain;
}
