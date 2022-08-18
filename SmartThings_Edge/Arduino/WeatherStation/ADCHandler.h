#ifndef adc_h
#define adc_h

#include "debug.h"

//
#define MAX_ADC_READING 4095
#define ADC_REF_VOLTAGE 3.3

//adc pins
#define VOLTAGE_PIN     32
#define LDR_PIN         35
#define MOISTURE_PIN    39
#define UV_PIN          34

//enable pins
#define UV_EN           27

//sample setup
#define ADC_SAMPLE_SEC     600                           //how often we're reading the sensor in seconds

//lux
#define LDR_REF_RESISTANCE      9910     
#define LUX_CALC_SCALAR         12500000
#define LUX_CALC_EXPONENT       -1.405

//define data structures
typedef struct ADCStructType
{
  float voltage;
  long ldr;
  String moisture;
  float uv;
};

class ADCHandler 
{

  public:
    void init();
    void storeSamples();
    float getVoltage();
    long getIllumination();
    String getMoisture();
    float getUV();
    
  private: 
    ADCStructType adcData;
    
    int readADC(int);
    long getIllum(int);
    double getVolts(int);
    double getUVIndex(int);
    String isWet(int);
    
};


#endif
