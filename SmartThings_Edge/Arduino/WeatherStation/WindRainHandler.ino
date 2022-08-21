#include <Arduino.h>

#include "WindRainHandler.h"
#include "WeatherStation.h"

void WindRainHandler::storeSamples(int sampleTime)
{
  //Store immediate values
  windSpeed=windRain.getWindSpeed(sampleTime);
  windDirection=calcWindDirection();
  windGustSpeed=windRain.getWindGustSpeed();
  rainRate=rainRate+windRain.getRainRate();

  //rolling averages  - https://stackoverflow.com/questions/10990618/calculate-rolling-moving-average-in-c/10990656#10990656
  float alpha = 1.0/(WIFITIME/(float)sampleTime);   //number of buckets to calculate over
  avgWindSpeed = (alpha * windSpeed) + (1.0 - alpha) * avgWindSpeed;

  VERBOSEPRINT("-in store samples- Wind Speed: (raw/avg) ");
  VERBOSEPRINT(windSpeed);  
  VERBOSEPRINT(" ");
  VERBOSEPRINT(avgWindSpeed); 
  VERBOSEPRINT("   - wind Direction: ");
  VERBOSEPRINTLN(windDirection);  

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

  VERBOSEPRINT("   Wind Direction: ");
  VERBOSEPRINTLN(direction);  

  return direction;
}

float WindRainHandler::getWindSpeed()
{
  VERBOSEPRINT("Wind Speed: (raw/avg) ");
  VERBOSEPRINT(windSpeed);  
  VERBOSEPRINT(" ");
  VERBOSEPRINTLN(avgWindSpeed); 
  
  return avgWindSpeed;
}
float WindRainHandler::getWindGustSpeed()
{ 
  VERBOSEPRINT("Wind Gust: (last/max) ");
  VERBOSEPRINT(windGustSpeed);  
  VERBOSEPRINT(" ");
  VERBOSEPRINTLN(maxGust); 

  windGustSpeed=maxGust;
  maxGust=0;  //reset since we're reading
 
  return windGustSpeed;
}
float WindRainHandler::getRainRate()
{
  rainRate=0;  //reset because we're reading
  return rainRate;
}
int WindRainHandler::getWindDirection()
{
  VERBOSEPRINT("Wind Dir: ");
  VERBOSEPRINTLN(windDirection);  
  
  return windDirection;
}
