#include "BME280Handler.h"

void setupBME()
{
    unsigned status;
    status = bme.begin();  
    if (!status) {
        ERRORPRINTLN("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
    }  
}

void storeBMESample()
{
  //Read BME
  BMEstruct bmeData;
  bmeData.temperature=bme.readTemperature();
  bmeData.pressure=bme.readPressure() / 100.0F;
  bmeData.humidity=bme.readHumidity();

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

//return max temperature
float getMaxTemperature()
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

//return min temperature
float getMinTemperature()
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
