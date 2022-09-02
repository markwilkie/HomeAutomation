#include "BME280Handler.h"
#include "EnvironmentCalculations.h"

extern Logger logger;
extern int currentTime();

void BME280Handler::init()
{
  //start things up
  unsigned status = bme.begin();  
  if (!status) {
      logger.log(ERROR,"Could not find a valid BME280 sensor");
  }  
}

void BME280Handler::storeSamples()
{
  //Read BME
  temperature=bme.readTemperature();
  pressure=bme.readPressure();
  humidity=bme.readHumidity();
  readingTime=currentTime();

  logger.log(VERBOSE,"Temperature: %f, Humidity: %f, Pressure: %f",temperature,humidity,pressure);
}

float BME280Handler::getTemperature()
{
  return (temperature*9.0/5.0) + 32.0;  //F
}

float BME280Handler::getPressure()
{
  return pressure/3386.3752577878;   //inHg 
}

float BME280Handler::getHumidity()
{
  return humidity;
}

long BME280Handler::getReadingTime()
{
  return readingTime;
}

float BME280Handler::getDewPoint()
{
  return EnvironmentCalculations::DewPoint(temperature, humidity, EnvironmentCalculations::TempUnit_Celsius);
}

float BME280Handler::getHeatIndex()
{
  return EnvironmentCalculations::HeatIndex(temperature, humidity, EnvironmentCalculations::TempUnit_Celsius);
}
