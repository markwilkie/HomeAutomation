#include <Arduino.h>

#include "WindRainHandler.h"
#include "WeatherStation.h"

void WindRainHandler::storeSamples()
{
  //Find out how long it's been since the last reading so we can calc speed correctly 
  long timeSinceLastReading = currentTime()-lastReadingTime;
  lastReadingTime=currentTime();
  
  //Store immediate values
  windSpeed=windRain.getWindSpeed(timeSinceLastReading);
  windDirection=calcWindDirection();
  windGustSpeed=windRain.getWindGustSpeed();
  rainRate=rainRate+windRain.getRainRate();

  //calc average
  totalSpeed=totalSpeed+windSpeed;
  speedSamples++;

  VERBOSEPRINT("In StoreSamples -");
  VERBOSEPRINT("  Wind Speed: (raw/avg) ");
  VERBOSEPRINT(windSpeed);  
  VERBOSEPRINT("/");
  VERBOSEPRINT(totalSpeed/(float)speedSamples); 
  VERBOSEPRINT("   - Gust Speed: (current/old max) ");
  VERBOSEPRINT(windGustSpeed);  
  VERBOSEPRINT("/");
  VERBOSEPRINT(maxGust);   
  VERBOSEPRINT("   - Wind Direction: ");
  VERBOSEPRINTLN(windDirection);  

  //max gust (but makes sure it's within bounds)
  if(windGustSpeed>(windSpeed*GUSTLIMIT))
  {
    ERRORPRINT("WARNING: Gust is over limit, which probably means glitch.  (gust/limit): ");
    ERRORPRINT(windGustSpeed);  
    ERRORPRINT("/");  
    ERRORPRINTLN(windSpeed);  
    ERRORPRINTLN("Setting gust to last wind speed instead."); 

    windGustSpeed=windSpeed;
  }
  if(windGustSpeed>maxGust)
    maxGust=windGustSpeed;  
}

int WindRainHandler::calcWindDirection()
{
  int smoothingLoop = 10;
  long adcValue = 0;
  for(int i=0;i<smoothingLoop;i++)
  {
    adcValue=adcValue+analogRead(WIND_VANE_PIN);
    delay(10);
  }
  adcValue=adcValue/smoothingLoop;
  
  VERBOSEPRINT("Wind vane raw ADC: ");
  VERBOSEPRINTLN(adcValue);

  //
  // the variable resistor in the voltage divider has a huge dead spot and is non-linear
  //
  int direction=0;
  if(adcValue > 3741 || adcValue <= 1)
    direction=0;
  if(adcValue > 12 && adcValue <= 405)
    direction=45;    
  if(adcValue > 330 && adcValue <= 805)
    direction=90;    
  if(adcValue > 805 && adcValue <= 1335)
    direction=135;    
  if(adcValue > 1335 && adcValue <= 1870)
    direction=180;
  if(adcValue > 1870 && adcValue <= 2410)
    direction=225;
  if(adcValue > 2300 && adcValue <= 3033)
    direction=270;
  if(adcValue > 3033 && adcValue <= 3741)
    direction=315;

  return direction;
}

float WindRainHandler::getWindSpeed()
{
  float avgWindSpeed=totalSpeed;
  if(speedSamples > 1) 
    totalSpeed/(float)speedSamples;

  totalSpeed=0;
  speedSamples=0;
  
  return avgWindSpeed;
}
float WindRainHandler::getWindGustSpeed()
{ 
  windGustSpeed=maxGust;
  maxGust=0;  //reset since we're reading
 
  return windGustSpeed;
}
float WindRainHandler::getRainRate()
{
  float currentRainRate=rainRate;
  rainRate=0;  //reset because we're reading
  return currentRainRate;
}
int WindRainHandler::getWindDirection()
{ 
  return windDirection;
}
