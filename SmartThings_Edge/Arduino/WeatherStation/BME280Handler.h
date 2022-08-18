#ifndef bme_h
#define bme_h

#include "debug.h"
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define SEALEVELPRESSURE_HPA (1013.25)

//sample setup
#define BME_SAMPLE_SEC     600                           //how often we're reading the sensor in seconds

typedef struct BMEStructType
{
  float temperature;
  float pressure;
  float humidity;
  long readingTime;
};

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
    Adafruit_BME280 bme; // I2C
    BMEStructType bmeData;

};


#endif
