#include <Arduino.h>

#include "WindRainHandler.h"
#include "WeatherStation.h"

void WindRainHandler::init()
{
  windSampleIdx=0;
  rainSampleIdx=0;
}

void WindRainHandler::storeSamples(int sampleTime)
{
  //Store immediate values
  windSpeed=windRain.getWindSpeed(sampleTime);
  windDirection=calcWindDirection();
  windGustSpeed=windRain.getWindGustSpeed();
  rainRate=windRain.getRainRate();

  //now store last 12
  GustStructType gustData;
  gustData.gust = windGustSpeed;
  gustData.gustDirection = windDirection;
  gustData.gustTime = epoch+(getSecondsSinceEpoch());
  gustLast12[windSampleIdx]=gustData;

  rainLast12[rainSampleIdx]=rainRate;

  //increment indexes
  windSampleIdx++;
  if(windSampleIdx>=GUST_LAST12_SIZE)
    windSampleIdx=0; 

  rainSampleIdx++;
  if(rainSampleIdx>=RAIN_LAST12_SIZE)
    rainSampleIdx=0; 
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
  VERBOSEPRINT(adcValue);

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

  VERBOSEPRINT("   Wind Direction: ");
  VERBOSEPRINTLN(direction);  

  return direction;
}

float WindRainHandler::getWindSpeed()
{
  return windSpeed;
}
float WindRainHandler::getWindGustSpeed()
{
  return windGustSpeed;
}
float WindRainHandler::getRainRate()
{
  return rainRate;
}
int WindRainHandler::getWindDirection()
{
  return windDirection;
}

int WindRainHandler::getLast12MaxGustIdx()
{
  int maxGustIdx=0;
  int maxGust=0;
  for(int idx=0;idx<GUST_LAST12_SIZE;idx++)
  {
    if(gustLast12[idx].gust>maxGust)
    {
      maxGust=gustLast12[idx].gust;
      maxGustIdx=idx;
    }
  }

  return maxGustIdx;
}

float WindRainHandler::getLast12MaxGustSpeed(int idx)
{
  return gustLast12[idx].gust;
}

int WindRainHandler::getLast12MaxGustDirection(int idx)
{
  return gustLast12[idx].gustDirection;
}

long WindRainHandler::getLast12MaxGustTime(int idx)
{
  return gustLast12[idx].gustTime;
}

float WindRainHandler::getRainRateLastHour()
{
  int idx=rainSampleIdx-((60*60)*WINDRAIN_SAMPLE_SEC);
  return rainLast12[idx];
}

float WindRainHandler::getMaxRainRateLast12()
{
  float maxRate=0;
  for(int idx=0;idx<RAIN_LAST12_SIZE;idx++)
  {
    if(rainLast12[idx]>maxRate)
    {
      maxRate=rainLast12[idx];
    }
  }
  return maxRate;
}

float WindRainHandler::getTotalRainLast12() 
{
  float totalRain=0;
  for(int idx=0;idx<RAIN_LAST12_SIZE;idx++)
  {
      totalRain=totalRain+rainLast12[idx];
  }

  return (totalRain/(RAIN_LAST12_SIZE/12));   //it's per hour....
}
