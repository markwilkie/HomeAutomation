#include "BME280Handler.h"
#include "EnvironmentCalculations.h"

void BME280Handler::init()
{
  //start things up
  unsigned status = bme.begin();  
  if (!status) {
      ERRORPRINTLN("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
  }  
  else
  {
    INFOPRINTLN("Found BME280!");
  }
}

void BME280Handler::storeSamples()
{
  //Read BME
  bmeData.temperature=bme.readTemperature();
  bmeData.pressure=bme.readPressure();
  bmeData.humidity=bme.readHumidity();
  bmeData.readingTime=currentTime();

  VERBOSEPRINT("Temperature: ");
  VERBOSEPRINT(bmeData.temperature);
  VERBOSEPRINT("  Humidity: ");
  VERBOSEPRINT(bmeData.humidity);
  VERBOSEPRINT("  Pressure: ");
  VERBOSEPRINTLN(bmeData.pressure);  
}

float BME280Handler::getTemperature()
{
  return (bmeData.temperature*9.0/5.0) + 32.0;  //F
}

float BME280Handler::getPressure()
{
  return bmeData.pressure/3386.3752577878;   //inHg 
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
