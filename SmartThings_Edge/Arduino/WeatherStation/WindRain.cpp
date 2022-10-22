#include "WindRain.h"
#include "WeatherStation.h"

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

//We usually doing last 12, last hour, etc on the hub.  But we're doing it at the ESP because pulses from the tipping bucket are few, so aggregating over time is more accurate.
void WindRain::getRainRate(double &current, double &lastHour, double &last12)
{
  //Add latest pulses to array which holds the last hour, then sum them
  long lastHourPulses=0;
  int pulsesSinceLastRead=0;
  if(currentTime() >= newCycleTime) 
  {
    //Get current, which is the value for each cycle time
    pulsesSinceLastRead = ulp.getULPRainPulseCount();
    current = (double)pulsesSinceLastRead*RAINFACTOR;

    lastHourRainPulseCount[currentIdx]=pulsesSinceLastRead;
    for(int i=0;i<(3600/CYCLETIME);i++) {
      lastHourPulses=lastHourPulses+lastHourRainPulseCount[i];
      logger.log(VERBOSE,"Last Hour Pulses: %d",lastHourRainPulseCount[i]);    
    }
    lastHour = (double)lastHourPulses*RAINFACTOR;

    //increment last hour index 
    currentIdx++;
    if(currentIdx>=3600/CYCLETIME)  {
      currentIdx=0;
    }
    newCycleTime=currentTime() + CYCLETIME;
  }

  //Has it been hour?  If so, add to array and sum that
  int pulsesLast12=0;
  if(currentTime() >= newHourTime) 
  {
    last12RainPulseCount[last12Idx]=lastHourPulses;
    for(int i=0;i<12;i++) {
      pulsesLast12=pulsesLast12+last12RainPulseCount[i];
      logger.log(VERBOSE,"Last 12 Pulses: %d",last12RainPulseCount[i]);   
    }    
    last12=(double)pulsesLast12*RAINFACTOR;

    last12Idx++;
    if(last12Idx>=12) {
      last12Idx=0;
    }
    newHourTime=currentTime() + 3600;
  }

  logger.log(VERBOSE,"Rain Pulses/Inches (current, hour, last12): %d/%f,  %d/%f,  %d/%f",pulsesSinceLastRead,current,lastHourPulses,lastHour,pulsesLast12,last12);
}
