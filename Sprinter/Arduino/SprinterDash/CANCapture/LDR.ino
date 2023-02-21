#include "LDR.h"

//
// There's an LDR in the LCD case which uses the 5v power that's there and a 10K pulldown for the analog output
//

//Pin used for analog reads
#define LDR_ADC_PIN A2
#define LDR_ADC_OVERSAMPLE 5

LDR::LDR(int _service,int _pid,int _refreshTicks)
{
  service=_service;
  pid=_pid;
  refreshTicks=_refreshTicks;
}

int LDR::getPid()
{
  return pid;
}

int LDR::getService()
{
  return service;
}

int LDR::readLightLevel()
{
    //don't update if it's not time to
    if(millis()<nextTickCount)
        return lightLevel;

    //Update timing
    nextTickCount=millis()+refreshTicks;

    //Read LDR
    for(int i=0;i<LDR_ADC_OVERSAMPLE;i++)
    {
      lightLevel = analogRead(LDR_ADC_PIN)*0.25 + lightLevel*0.75;
      delay(10);
    }

    Serial.print("LDR:  ");
    Serial.println(lightLevel);

    return lightLevel;
}
