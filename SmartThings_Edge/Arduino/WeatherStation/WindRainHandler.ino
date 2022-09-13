#include <Arduino.h>

#include "WindRainHandler.h"
#include "WeatherStation.h"

extern double round2(double);

void WindRainHandler::storeSamples()
{
  //Find out how long it's been since the last reading so we can calc speed correctly 
  long timeSinceLastReading = currentTime()-lastReadingTime;
  lastReadingTime=currentTime();
  
  //Store immediate values
  windSpeed=windRain.getWindSpeed(timeSinceLastReading);
  rawDirectioninDegrees=calcWindDirection();
  windGustSpeed=windRain.getWindGustSpeed();
  rainRate=rainRate+windRain.getRainRate();

  //calc average
  totalSpeed=totalSpeed+windSpeed;
  speedSamples++;

  logger.log(VERBOSE,"Samples - Wind Speed: (raw/avg) %f/%f, Gust Speed: (current/old max) %f/%f, Direction: %d",windSpeed,totalSpeed/(double)speedSamples,windGustSpeed,maxGust,rawDirectioninDegrees);

  //max gust (but makes sure it's within bounds)
  if(windGustSpeed>(windSpeed*GUSTLIMIT))
  {
    logger.log(WARNING,"Gust is over limit, setting to last wind speed.  (gust/limit): %f/%f",windGustSpeed,windSpeed*GUSTLIMIT);
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
  logger.log(VERBOSE,"Wind vane raw ADC: %d",adcValue);

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

double WindRainHandler::getWindSpeed()
{
  double avgWindSpeed=totalSpeed;
  if(speedSamples > 1) 
    totalSpeed/(double)speedSamples;

  totalSpeed=0;
  speedSamples=0;
  
  return round2(avgWindSpeed);
}
double WindRainHandler::getWindGustSpeed()
{ 
  windGustSpeed=maxGust;
  maxGust=0;  //reset since we're reading
 
  return round2(windGustSpeed);
}
double WindRainHandler::getRainRate()
{
  double currentRainRate=rainRate;
  rainRate=0;  //reset because we're reading
  return round2(currentRainRate);
}
int WindRainHandler::getDirectionInDeg()
{
  int dir=rawDirectioninDegrees+WIND_DIR_OFFSET;
  if(dir>359)
    dir=dir-359;
  if(dir<0)
    dir=dir+359;

  logger.log(VERBOSE,"Wind Direction adjusted: %d",dir);      

  return dir;
}

String WindRainHandler::getDirectionLabel()
{ 
  String dirLabel="-";
  int dir=getDirectionInDeg();
  
  if(dir>=338 || dir<=22) 
    dirLabel="N";
  if(dir>22 && dir<68) 
    dirLabel="NE";   
  if(dir>=68 && dir<=112) 
    dirLabel="E";    
  if(dir>112 && dir<158) 
    dirLabel="SE";    
  if(dir>=158 && dir<=202) 
    dirLabel="S";    
  if(dir>202 && dir<248) 
    dirLabel="SW";  
  if(dir>=248 && dir<=292) 
    dirLabel="W";     
  if(dir>292 && dir<338) 
    dirLabel="NW";  

  logger.log(VERBOSE,"Wind Direction Label: %s",dirLabel.c_str());    

  return dirLabel;
}