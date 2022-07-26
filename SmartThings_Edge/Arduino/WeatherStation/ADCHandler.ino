#include "ADCHandler.h"

void setupADC()
{
  pinMode(CAP_VOLTAGE_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(UV_PIN, INPUT);

  pinMode(UV_EN, OUTPUT);
}

void storeADCSample()
{
  //Enable stuff that needs it
  digitalWrite(UV_EN, HIGH);
  
  //Read ADC
  ADCstruct adcData;
  adcData.capVoltage=getVolts(readADC(CAP_VOLTAGE_PIN)*2);   //doubled because it's gone through a 10x10K divider
  adcData.ldr=map(readADC(LDR_PIN), 0, 4095, 0, 100000);   //1-100000 brightness
  adcData.moisture=isWet(readADC(MOISTURE_PIN));
  adcData.uv=getUVIndex(readADC(UV_PIN));

  //Disable stuff that needs it
  digitalWrite(UV_EN, LOW);

  //Add to sample array
  adcSamples[adcSampleIdx]=adcData;
  adcSampleIdx++;

  //If wraps, then reset 
  if(adcSampleIdx>=ADC_LAST12_SIZE)
  {
    //reset pos
    adcSampleIdx=0;  
  }
}

int readADC(int pin)
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

double getVolts(int adcReading)
{
  return 3.3 / 4095 * adcReading;
}

double getUVIndex(int adcReading)
{
  //used to map voltage to intensity
  double in_min=.99;
  double in_max=2.8;
  double out_min=0.0;
  double out_max=15.0;
  
  double outputVoltage = getVolts(adcReading);
  double uvIntensity = (outputVoltage - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
  double index=uvIntensity * 1.61;

  return index;
}

String isWet(int adcReading)
{
  if(adcReading > 10)
    return "wet";
  else
    return "dry";
}

//return max cap voltage
int getMaxCapVoltage()
{
  int maxVoltage=0;
  for(int idx=0;idx<ADC_LAST12_SIZE;idx++)
  {
    if(adcSamples[idx].capVoltage>maxVoltage)
    {
      maxVoltage=adcSamples[idx].capVoltage;
    }
  }

  return maxVoltage;
}

//return min cap voltage
int getMinCapVoltage()
{
  int minVoltage=0;
  for(int idx=0;idx<ADC_LAST12_SIZE;idx++)
  {
    if(adcSamples[idx].capVoltage<minVoltage)
    {
      minVoltage=adcSamples[idx].capVoltage;
    }
  }

  return minVoltage;
}
