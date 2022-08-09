#include "BME280Handler.h"
#include "EnvironmentCalculations.h"

BME280Handler::BME280Handler()
{
  //start things up
  unsigned status = bme.begin();  
  if (!status) {
      ERRORPRINTLN("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
  }  
}

void BME280Handler::storeSamples()
{
  //Read BME
  bmeData.temperature=bme.readTemperature();
  bmeData.pressure=bme.readPressure()/3386.3752577878;          // convert to inches of mercury  -  pa * 1inHg/3386.3752577878Pa */;
  bmeData.humidity=bme.readHumidity();
  bmeData.readingTime=epoch+getSecondsSinceEpoch();

  //Add to sample array
  bmeSamples[bmeSampleIdx]=bmeData;
  bmeSampleIdx++;

  //If wraps, then reset AND store off high gust
  if(bmeSampleIdx>=BME_LAST12_SIZE)
  {
    //reset pos
    bmeSampleIdx=0;  
  }
}

float BME280Handler::getTemperature()
{
  return bmeData.temperature;
}

float BME280Handler::getPressure()
{
  return bmeData.pressure;
}

float BME280Handler::getHumidity()
{
  return bmeData.humidity;
}

long BME280Handler::getReadingTime()
{
  return bmeData.readingTime;
}

float BME280Handler::getDewPoint()
{
  return EnvironmentCalculations::DewPoint(bmeData.temperature, bmeData.humidity, EnvironmentCalculations::TempUnit_Celsius);
}

float BME280Handler::getHeatIndex()
{
  return EnvironmentCalculations::HeatIndex(bmeData.temperature, bmeData.humidity, EnvironmentCalculations::TempUnit_Celsius);
}

//return temperature delta for the last hour
float BME280Handler::getTemperatureChange()
{
  //Find out which slot was an hour ago
  int idxHour=(60*60)/BME_SAMPLE_SEC;  //how many "slots" each hour takes up
  int lastHourIdx=bmeSampleIdx-idxHour;
  if(lastHourIdx<0)
    lastHourIdx=BME_LAST12_SIZE-lastHourIdx; 

  //return delta  
  return bmeSamples[bmeSampleIdx].temperature-bmeSamples[lastHourIdx].temperature;
}

//return pressure delta for the last hour
float BME280Handler::getPressureChange()
{
  //Find out which slot was an hour ago
  int idxHour=(60*60)/BME_SAMPLE_SEC;  //how many "slots" each hour takes up
  int lastHourIdx=bmeSampleIdx-idxHour;
  if(lastHourIdx<0)
    lastHourIdx=BME_LAST12_SIZE-lastHourIdx; 

  //return delta  
  return bmeSamples[bmeSampleIdx].pressure-bmeSamples[lastHourIdx].pressure;
}

//return max temperature 
float BME280Handler::getMaxTemperature()
{
  float maxTemperature=0;
  for(int idx=0;idx<BME_LAST12_SIZE;idx++)
  {
    if(bmeSamples[idx].temperature>maxTemperature)
    {
      maxTemperature=bmeSamples[idx].temperature;
    }
  }
  return maxTemperature;
}

long BME280Handler::getMaxTemperatureTime()
{
  long readingTime=0;
  float maxTemperature=0;
  for(int idx=0;idx<BME_LAST12_SIZE;idx++)
  {
    if(bmeSamples[idx].temperature>maxTemperature)
    {
      maxTemperature=bmeSamples[idx].temperature;
      readingTime=bmeSamples[idx].readingTime;
    }
  }
  return readingTime;
}

//return min temperature
float BME280Handler::getMinTemperature()
{
  float minTemperature=0;  
  for(int idx=0;idx<BME_LAST12_SIZE;idx++)
  {
    if(bmeSamples[idx].temperature<minTemperature)
    {
      minTemperature=bmeSamples[idx].temperature;
    }
  }
  return minTemperature;
}

//return min temperature
long BME280Handler::getMinTemperatureTime()
{
  long readingTime=0;
  float minTemperature=0;  
  for(int idx=0;idx<BME_LAST12_SIZE;idx++)
  {
    if(bmeSamples[idx].temperature<minTemperature)
    {
      minTemperature=bmeSamples[idx].temperature;
      readingTime=bmeSamples[idx].readingTime;
    }
  }
  return readingTime;
}
