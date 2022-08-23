#ifndef bme_h
#define bme_h

#include "logger.h"
#include "debug.h"
#include <Wire.h>
#include <BME280I2C.h>

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

    long getReadingTime();
    
  private: 
    BME280I2C bme; // I2C

    float temperature;
    float pressure;
    float humidity;
    long readingTime;    
};


#endif
