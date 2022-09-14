#include "ADCHandler.h"

extern Logger logger;
extern double round2(double);

void ADCHandler::init()
{
  pinMode(CAP_VOLTAGE_PIN, INPUT);
  pinMode(VCC_VOLTAGE_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(UV_PIN, INPUT);

  pinMode(UV_EN, OUTPUT);
}

void ADCHandler::storeSamples()
{
  //Enable stuff that needs it
  digitalWrite(UV_EN, HIGH);
  delay(100);

  //raw
  _capVoltage = readADC(CAP_VOLTAGE_PIN);
  _vccVoltage = readADC(VCC_VOLTAGE_PIN);
  _ldr = readADC(LDR_PIN);
  _moisture = readADC(MOISTURE_PIN);
  _uv = readADC(UV_PIN);

  logger.log(VERBOSE,"RAW ADC VALUES: cap voltage: %d, vcc voltage: %d, LDR: %d, Moisture: %d, UV: %d",_capVoltage,_vccVoltage,_ldr,_moisture,_uv);  

  //Disable stuff that needs it
  digitalWrite(UV_EN, LOW);
}

double ADCHandler::getCapVoltage()
{
  double voltage=getVolts(_capVoltage*2);  //doubled because it's gone through a 10x10K divider;
  voltage=voltage*CAP_VOLTAGE_CALIB;
  
  return round2(voltage);   
}

double ADCHandler::getVCCVoltage()
{
  double voltage=getVolts(_vccVoltage);
  voltage=voltage*VCC_VOLTAGE_CALIB;     //account for the voltage divider
  
  return round2(voltage);   
}

long ADCHandler::getIllumination()
{
  long ldr=getIllum(_ldr);   //1-100000 brightness;
  
  return ldr;
}

String ADCHandler::getMoisture()
{
  String moisture=isWet(_moisture);
    
  return moisture;
}

double ADCHandler::getUV()
{
  double uv=getUVIndex(_uv);
    
  return round2(uv);
}

int ADCHandler::readADC(int pin)
{
  int tries=0; 
  double sampleSum = 0;
  double mean = 0.0;
  double sqDevSum = 0.0;
  double stdDev = 0.0;
  double tolerance = 0.0;

  //Try to get a clean read that is within tolerance
  while(tries<MAXTRIES)
  {
    tries++;

    //reset for retries
    sampleSum = 0;
    mean = 0.0;
    sqDevSum = 0.0;
    stdDev = 0.0;
    tolerance = 0.0;    

    //Read samples and sum them
    for(int i = 0; i < SAMPLES; i++) {
      sampleValues[i] = analogRead(pin);
      sampleSum += sampleValues[i];
      delay(WAITTIME); // set this to whatever you want
    }

    //calc mean
    mean = sampleSum/double(SAMPLES);

    //calc std deviation  (throw out bad reads)
    for(int i = 0; i < SAMPLES; i++) 
    {
      sqDevSum += pow((mean - double(sampleValues[i])), 2);
    }
    stdDev = sqrt(sqDevSum/double(SAMPLES));  

    //are we within tolerance?
    tolerance = (MAX_ADC_READING*(TOLERANCE/100.0));
    if(stdDev<tolerance)
      break;

    logger.log(WARNING,"ADC read's standard deviation was out of tolerance - trying again (pin/avg/stdDev) %d/%f/%f",pin,mean,stdDev);
    delay(500); 
  }

  //Throw error if we're messed up
  if(stdDev>tolerance)
  {
    logger.log(WARNING,"ADC read's standard deviation was out of tolerance - returning zero: (pin/avg/stdDev) %d/%f/%f",pin,mean,stdDev);
    mean=0;
  }

  return(mean);
}

long ADCHandler::getIllum(int ldr_raw_data)
{
  double ldr_voltage;
  double ldr_resistor_voltage;
  double ldr_resistance;
  double ldr_lux;

  //Convert adc reading to volts
  ldr_voltage = getVolts(ldr_raw_data);

  //Now get voltage of the LDR itself 
  ldr_resistor_voltage = ADC_REF_VOLTAGE - ldr_voltage;
  if(ldr_resistor_voltage<=0)
    return 100000;  //means the adc is at 4095, or as bright as it gets

  //Now we can find the actual resistance of the ldr
  ldr_resistance = (ldr_resistor_voltage/ldr_voltage)*LDR_REF_RESISTANCE;
  
  // LDR LUX
  ldr_lux = LUX_CALC_SCALAR * pow(ldr_resistance, LUX_CALC_EXPONENT);

  if(ldr_lux>100000)
    ldr_lux=100000;   //this is the max that is acceptable

  return ldr_lux;
}

double ADCHandler::getVolts(int adcReading)
{
  return ((double)adcReading / MAX_ADC_READING) * ADC_REF_VOLTAGE;
}

double ADCHandler::getUVIndex(int adcReading)
{
  //used to map voltage to intensity
  double in_min=.99;
  double in_max=2.8;
  double out_min=0.0;
  double out_max=15.0;
  
  double outputVoltage = getVolts(adcReading);
  double uvIntensity = (outputVoltage - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
  double index=uvIntensity * 1.61; 

  if(index<0)
    index=0;

  return index;
}

String ADCHandler::isWet(int adcReading)
{
  if(adcReading > WET_THRESHOLD)
    return "wet";
  else
    return "dry";
}
