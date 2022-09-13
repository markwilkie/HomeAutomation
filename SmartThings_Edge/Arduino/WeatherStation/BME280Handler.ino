#include "BME280Handler.h"
#include "EnvironmentCalculations.h"

extern Logger logger;
extern unsigned long currentTime();
extern double round2(double);

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

double BME280Handler::getTemperature()
{
  return round2((temperature*9.0/5.0) + 32.0);  //F
}

double BME280Handler::getPressure()
{
  return round2(pressure/3386.3752577878);   //inHg 
}

double BME280Handler::getHumidity()
{
  return round2(humidity);
}

unsigned long BME280Handler::getReadingTime()
{
  return readingTime;
}

double BME280Handler::getDewPoint()
{
  return round2(EnvironmentCalculations::DewPoint(temperature, humidity, EnvironmentCalculations::TempUnit_Celsius));
}

double BME280Handler::getHeatIndex()
{
  return round2(EnvironmentCalculations::HeatIndex(temperature, humidity, EnvironmentCalculations::TempUnit_Celsius));
}
