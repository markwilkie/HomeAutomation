#ifndef adc_h
#define adc_h

#include "debug.h"

//adc pins
#define VOLTAGE_PIN 32
#define LDR_PIN         35
#define MOISTURE_PIN    39
#define UV_PIN          34

//enable pins
#define UV_EN           27

//sample setup
#define ADC_SAMPLE_SEC     600                           //how often we're reading the sensor in seconds
#define ADC_LAST12_SIZE    ((12*60*60)/BME_SAMPLE_SEC)   //# of samples we need to store for last 12

//define data structures
typedef struct adcStruct
{
  float voltage;
  long ldr;
  String moisture;
  float uv;
} ADCstruct;

static ADCstruct adcSamples[ADC_LAST12_SIZE];     

int adcSeconds = 0; //timer gets called every second, but we only want to read and clear every SAMPLE_SIZE
int adcSampleIdx = 0;              //index for where in the sample array we are

#endif
