#include "WindRain.h"
#include "WeatherStation.h"

//Time since the last reading is passed in
double WindRain::getWindSpeed(long timeSinceLastReading)
{ 
  int windPulsesRaw = ulp.getULPWindPulseCount();
  double wind = (((double)windPulsesRaw / (double)timeSinceLastReading) / 20.0) * WINDFACTOR;  //there's 20 pulses per revolution 
  logger.log(VERBOSE,"Wind Pulses: %d, Time Elpased: %d, Calc'd speed: %f",windPulsesRaw,timeSinceLastReading,wind);

  return wind;
}

double WindRain::getWindGustSpeed()
{
  uint32_t shortestWindPulseTime = ulp.getULPShortestWindPulseTime();
  if(shortestWindPulseTime==0)
    return 0;

  //figure gust
  double revPerSec = ((TIMEFACTOR/shortestWindPulseTime)/20.0) * GUSTFACTOR;  
  double windGust = revPerSec * WINDFACTOR; 

  logger.log(VERBOSE,"Shortest wind pulse Time (ms): %d, calc'd speed: %f",(int)shortestWindPulseTime,windGust);

  return windGust;
}

//We usually doing last 12, last hour, etc on the hub.  But we're doing it at the ESP because pulses from the tipping bucket are few, so aggregating over time is more accurate.
void WindRain::getRainRate(double &current, double &lastHour, double &last12)
{
  //Not cycle time yet, just return
  if(currentTime() < newCycleTime) {
    return;
  }

  //Get current, which is the value for each cycle time
  int pulsesSinceLastRead = ulp.getULPRainPulseCount();
  current = (double)pulsesSinceLastRead*RAINFACTOR;

  //Add current to last hour and sum that up
  lastHourRainPulseCount[currentIdx]=pulsesSinceLastRead;
  logger.log(VERBOSE,"Adding rain to hour: %d, current index: %d, current cycle Time: %s",pulsesSinceLastRead,currentIdx,getTimeString(newCycleTime)); 
  long lastHourPulses=0;
  for(int i=0;i<(3600/CYCLETIME);i++) {
    lastHourPulses=lastHourPulses+lastHourRainPulseCount[i];
  }
  lastHour = (double)lastHourPulses*RAINFACTOR;

  //increment last hour index and set new cycle time
  currentIdx++;
  if(currentIdx>=3600/CYCLETIME)  {
    currentIdx=0;
  }
  newCycleTime=currentTime() + CYCLETIME;

  //Sum last12 and then add last hour so we're up to date
  int pulsesLast12=0;
  for(int i=0;i<12;i++) {
    pulsesLast12=pulsesLast12+last12RainPulseCount[i]; 
  }
  pulsesLast12=pulsesLast12+lastHourPulses;
  last12=(double)pulsesLast12*RAINFACTOR;


  //Has it been hour?  If so, add to array
  if(currentTime() >= newHourTime) 
  {
    logger.log(VERBOSE,"Adding rain to last12: %d, current index: %d, current hour time: %s",lastHourPulses,last12Idx,getTimeString(newHourTime));  
    last12RainPulseCount[last12Idx]=lastHourPulses;

    last12Idx++;
    if(last12Idx>=12) {
      last12Idx=0;
    }
    newHourTime=currentTime() + 3600;
  }

  logger.log(VERBOSE,"Rain Pulses/Inches: %s (current, hour, last12): %d/%f,  %d/%f,  %d/%f",getTimeString(currentTime()),pulsesSinceLastRead,current,lastHourPulses,lastHour,pulsesLast12,last12);
}
