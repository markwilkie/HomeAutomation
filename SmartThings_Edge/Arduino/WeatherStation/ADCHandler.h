#ifndef adc_h
#define adc_h

#include "logger.h"
#include "debug.h"

//
#define MAX_ADC_READING 4095
#define ADC_REF_VOLTAGE 3.3  
#define SAMPLES   10          //number of samples to take
#define WAITTIME  20          //time to wait between samples in ms
#define TOLERANCE 5           //% of max adc reading to e used to determine if we need to resample
#define MAXTRIES  3           //# of times we'll try and get a clean read that is within tolerance  

//MUX selector pins
#define MUX_EN  27          //low is enabled
#define MUX_SEL_0  2
#define MUX_SEL_1  5

//adc pins
// Note that 15 doesn't work when wifi is enabled
#define CAP_VOLTAGE_PIN     39    
#define VCC_VOLTAGE_PIN     35    
#define MOISTURE_PIN        13   // GPIO 6-11 can't be used for digital inputs
#define MUX_PIN             34   // either one of the 3 UV sensors or the LDR

//sample setup
#define ADC_SAMPLE_SEC     600                //how often we're reading the sensor in seconds

//lux
#define LDR_REF_RESISTANCE      10000         //is the other resistor in the divider     
#define LUX_CALC_SCALAR         12500000
#define LUX_CALC_EXPONENT       -1.405

//voltage
#define CAP_VOLTAGE_CALIB           1.05          //calibration for calc'ing voltage
#define VCC_VOLTAGE_CALIB       1.69

class ADCHandler 
{

  public:
    void init();
    void storeSamples();
    double getCapVoltage();
    double getVCCVoltage();
    long getIllumination();
    String getMoisture();
    double getUV();
    double getUV1();
    double getUV2();
    double getUV3();
    
  private: 
    int readADC(int);
    int readDigital(int);
    long getIllum(int);
    double getVolts(int);
    double getUVIndex(int);
    String isWet(int);

    int _capVoltage;
    int _vccVoltage;
    int _ldr;
    int _moisture;
    int _uv1;    
    int _uv2;    
    int _uv3;    

    long sampleValues[SAMPLES];
};


#endif
