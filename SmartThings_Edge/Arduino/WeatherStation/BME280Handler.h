#ifndef bme_h
#define bme_h

#include "logger.h"
#include "debug.h"
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define SEALEVELPRESSURE_HPA (1013.25)

class BME280Handler 
{

  public:
    void init();
    void storeSamples();
    double getTemperature();
    double getPressure();
    double getHumidity();
    double getDewPoint();
    double getHeatIndex(); 

    unsigned long getReadingTime();
    
  private: 
    Adafruit_BME280 bme; // I2C

    double temperature;
    double pressure;
    double humidity;
    unsigned long readingTime;    
};


#endif
