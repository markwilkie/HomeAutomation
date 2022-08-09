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
#define BME_LAST12_SIZE   ((12*60*60)/BME_SAMPLE_SEC)    //# of samples we need to store for last 12

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
    BME280Handler();
    void storeSamples();
    float getTemperature();
    float getPressure();
    float getHumidity();
    float getDewPoint();
    float getHeatIndex();
    
    float getMaxTemperature();
    long getMaxTemperatureTime();
    float getMinTemperature();
    long getMinTemperatureTime();
    float getTemperatureChange();
    float getPressureChange();  
    long getReadingTime();
    
  private: 
    struct BMEStructType bmeSamples[BME_LAST12_SIZE];
    Adafruit_BME280 bme; // I2C
    BMEStructType bmeData;

    int bmeSampleIdx;              //index for where in the sample array we are
};


#endif
