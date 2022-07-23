#ifndef bme_h
#define bme_h

#include "debug.h"
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

Adafruit_BME280 bme; // I2C

#define SEALEVELPRESSURE_HPA (1013.25)

//sample setup
#define BME_SAMPLE_SEC     600                           //how often we're reading the sensor in seconds
#define BME_LAST12_SIZE    ((12*60*60)/BME_SAMPLE_SEC)   //# of samples we need to store for last 12

//define data structures
typedef struct bmeStruct
{
  float temperature;
  float pressure;
  float humidity;
} BMEstruct;

static BMEstruct bmeSamples[BME_LAST12_SIZE];     

int bmeSeconds = 0;                //timer gets called every second, but we only want to read and clear every SAMPLE_SIZE
int bmeSampleIdx = 0;              //index for where in the sample array we are

#endif
