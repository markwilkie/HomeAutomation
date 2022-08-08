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
  bmeData.pressure=bme.readPressure()/3386.3752577878;          // convert to inches of mercury  -  pa * 1inHg/3386.3752577878Pa */;
  bmeData.humidity=bme.readHumidity();
  bmeData.readingTime=epoch+secondsSinceEpoch;

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

//return temperature delta for the last hour
float getTemperatureChange()
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
float getPressureChange()
{
  //Find out which slot was an hour ago
  int idxHour=(60*60)/BME_SAMPLE_SEC;  //how many "slots" each hour takes up
  int lastHourIdx=bmeSampleIdx-idxHour;
  if(lastHourIdx<0)
    lastHourIdx=BME_LAST12_SIZE-lastHourIdx; 

  //return delta  
  return bmeSamples[bmeSampleIdx].pressure-bmeSamples[lastHourIdx].pressure;
}

//return max temperature index
int getMaxTemperatureIndex()
{
  float maxTemperature=0;
  int maxIndex=0;
  for(int idx=0;idx<BME_LAST12_SIZE;idx++)
  {
    if(bmeSamples[idx].temperature>maxTemperature)
    {
      maxTemperature=bmeSamples[idx].temperature;
      maxIndex=idx;
    }
  }

  return maxIndex;
}

//return min temperature
int getMinTemperatureIndex()
{
  float minTemperature=0;
  int minIndex=0;
  for(int idx=0;idx<BME_LAST12_SIZE;idx++)
  {
    if(bmeSamples[idx].temperature<minTemperature)
    {
      minTemperature=bmeSamples[idx].temperature;
      minIndex=idx;
    }
  }

  return minIndex;
}
