#include "BME280Handler.h"
#include "EnvironmentCalculations.h"

void BME280Handler::init()
{
  //start things up
  Wire.begin();
  unsigned status = bme.begin();  
  if (!status) {
      ERRORPRINTLN("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
  }  
}

void BME280Handler::storeSamples()
{
  //Read BME
  BME280::TempUnit tempUnit(BME280::TempUnit_Fahrenheit );
  BME280::PresUnit presUnit(BME280::PresUnit_Pa);
  bme.read(pressure, temperature, humidity, tempUnit, presUnit);
  
  readingTime=currentTime();

  VERBOSEPRINT("Temperature: ");
  VERBOSEPRINT(temperature);
  VERBOSEPRINT("  Humidity: ");
  VERBOSEPRINT(humidity);
  VERBOSEPRINT("  Pressure: ");
  VERBOSEPRINTLN(pressure);  
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
