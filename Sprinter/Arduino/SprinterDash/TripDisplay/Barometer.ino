#include "Barometer.h"

void Barometer::init(int _refreshTicks)
{
    refreshTicks=_refreshTicks;
}

void Barometer::setup()
{
  if (!baro.begin()) 
  {
    Serial.println("Could not find MPL3115A2 Barometer");
  }
}

void Barometer::update()
{
  //don't update if it's not time to
  if(millis()<nextTickCount)
      return;
  nextTickCount=millis()+refreshTicks;

  //Is there a reading ready?
  readingReady=baro.conversionComplete();

  //Let's check if we need to start a new read or not
  if(!readingStarted && !readingReady)
  {
      //Serial.println("Starting non blocking barometer read");
      if(readingPressureNow)
          baro.setMode(MPL3115A2_BAROMETER);
      else
          baro.setMode(MPL3115A2_ALTIMETER);

      baro.startOneShot();
      return;
  }

  //If we're not quite ready, then come back
  if(readingStarted && !readingReady)
  {
    Serial.println("Baro not quite ready yet");
    return;
  }

  //Looks like our results are ready!
  if(readingPressureNow)
  {
    pressure = baro.getLastConversionResults(MPL3115A2_PRESSURE)/10.0;
    readingStarted=false;
    readingReady=false;
    readingPressureNow=false;

    //Serial.print("Pressure = ");
    //Serial.println(pressure);
  }
  else
  {
    elevation = baro.getLastConversionResults(MPL3115A2_ALTITUDE)* 3.28084;
    readingStarted=false;
    readingReady=false;
    readingPressureNow=true;

    //Serial.print("Altitude = ");
    //Serial.println(elevation); 
  }
}

int Barometer::getElevation()
{
  return elevation;
}

double Barometer::getPressure()
{
  return pressure;
}
