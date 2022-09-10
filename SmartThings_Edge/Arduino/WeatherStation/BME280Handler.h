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
    float getTemperature();
    float getPressure();
    float getHumidity();
    float getDewPoint();
    float getHeatIndex(); 

    unsigned long getReadingTime();
    
  private: 
    Adafruit_BME280 bme; // I2C

    float temperature;
    float pressure;
    float humidity;
    unsigned long readingTime;    
};


#endif
