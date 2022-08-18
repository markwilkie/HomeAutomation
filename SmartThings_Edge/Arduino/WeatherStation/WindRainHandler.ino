#include <Arduino.h>

#include "WindRainHandler.h"
#include "WeatherStation.h"

void WindRainHandler::storeSamples(int sampleTime)
{
  //Store immediate values
  windSpeed=windRain.getWindSpeed(sampleTime);
  windDirection=calcWindDirection();
  windGustSpeed=windRain.getWindGustSpeed();
  rainRate=windRain.getRainRate();

  //rolling averages  - https://stackoverflow.com/questions/10990618/calculate-rolling-moving-average-in-c/10990656#10990656
  float alpha = 1.0/(WIFITIME/WINDTIME);   //number of buckets to calculate over
  avgWindSpeed = (alpha * windSpeed) + (1.0 - alpha) * avgWindSpeed;
  avgWindDirection = (alpha * windDirection) + (1.0 - alpha) * avgWindDirection;
  avgRainRate = (alpha * rainRate) + (1.0 - alpha) * avgRainRate;  

  //max gust
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
  return avgRainRate;
}
int WindRainHandler::getWindDirection()
{
  return avgWindDirection;
}
