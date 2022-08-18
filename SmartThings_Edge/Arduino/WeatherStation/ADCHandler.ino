#include "ADCHandler.h"

void ADCHandler::init()
{
  pinMode(VOLTAGE_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(UV_PIN, INPUT);

  pinMode(UV_EN, OUTPUT);
}

void ADCHandler::storeSamples()
{
  //Enable stuff that needs it
  digitalWrite(UV_EN, HIGH);
  
  //Read ADC
  adcData.voltage=getVolts(readADC(VOLTAGE_PIN)*2);   //doubled because it's gone through a 10x10K divider
  adcData.ldr=getIllum(readADC(LDR_PIN));   //1-100000 brightness
  adcData.moisture=isWet(readADC(MOISTURE_PIN));
  adcData.uv=getUVIndex(readADC(UV_PIN));

  //Disable stuff that needs it
  digitalWrite(UV_EN, LOW);
}

float ADCHandler::getVoltage()
{
  return adcData.voltage;
}

long ADCHandler::getIllumination()
{
  return adcData.ldr;
}

String ADCHandler::getMoisture()
{
  return adcData.moisture;
}

float ADCHandler::getUV()
{
  return adcData.uv;
}

int ADCHandler::readADC(int pin)
{
  int readNum=10;
  long sum=0;
  for(int i=0;i<readNum;i++)
  {
    sum=sum+analogRead(pin);
    delay(1);
  }

  return(sum/readNum);
}

long ADCHandler::getIllum(int ldr_raw_data)
{
  float ldr_voltage;
  float ldr_resistance;
  float ldr_lux;

  // LDR VOLTAGE CONVERSION
  // Convert the raw digital data back to the voltage that was measured on the analog pin
  ldr_voltage = (float) ldr_raw_data / MAX_ADC_READING * ADC_REF_VOLTAGE;

  // LDR RESISTANCE CONVERSION
  // resistance that the LDR would have for that voltage
  ldr_resistance = (ldr_voltage * LDR_REF_RESISTANCE) / (ADC_REF_VOLTAGE - ldr_voltage);

  // LDR LUX
  // Change the code below to the proper conversion from ldr_resistance to ldr_lux
  ldr_lux = LUX_CALC_SCALAR * pow(ldr_resistance, LUX_CALC_EXPONENT);

  return ldr_lux;
}

double ADCHandler::getVolts(int adcReading)
{
  return ADC_REF_VOLTAGE / MAX_ADC_READING * adcReading;
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
  if(adcReading > 20)
    return "wet";
  else
    return "dry";
}
