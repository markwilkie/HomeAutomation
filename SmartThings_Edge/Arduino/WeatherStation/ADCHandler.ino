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
  delay(100);

  //raw
  _voltage = readADC(VOLTAGE_PIN);
  _ldr = readADC(LDR_PIN);
  _moisture = readADC(MOISTURE_PIN);
  _uv = readADC(UV_PIN);

  VERBOSEPRINT("RAW ADC VALUES: voltage: ");
  VERBOSEPRINT(_voltage);
  VERBOSEPRINT("  ldr: ");
  VERBOSEPRINT(_ldr);
  VERBOSEPRINT("  moisture: ");
  VERBOSEPRINT(_moisture);  
  VERBOSEPRINT("  uv: ");
  VERBOSEPRINTLN(_uv);     

  //Disable stuff that needs it
  digitalWrite(UV_EN, LOW);
}

float ADCHandler::getVoltage()
{
  float voltage=getVolts(_voltage*2);  //doubled because it's gone through a 10x10K divider;
  VERBOSEPRINT("Voltage: ");
  VERBOSEPRINT(voltage);
  
  return voltage;   
}

long ADCHandler::getIllumination()
{
  long ldr=getIllum(_ldr);   //1-100000 brightness;
  VERBOSEPRINT("LDR: ");
  VERBOSEPRINTLN(ldr);
  
  return ldr;
}

String ADCHandler::getMoisture()
{
  String moisture=isWet(_moisture);
  VERBOSEPRINT("Moisture: ");
  VERBOSEPRINTLN(moisture); 
    
  return moisture;
}

float ADCHandler::getUV()
{
  float uv=getUVIndex(_uv);
  VERBOSEPRINT("UV: ");
  VERBOSEPRINTLN(uv);  
    
  return uv;
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
  float ldr_resistor_voltage;
  float ldr_resistance;
  float ldr_lux;

  //Convert adc reading to volts
  ldr_voltage = getVolts(ldr_raw_data);

  //Now get voltage of the LDR itself 
  ldr_resistor_voltage = ADC_REF_VOLTAGE - ldr_voltage;

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
  return ((float)adcReading / MAX_ADC_READING) * ADC_REF_VOLTAGE;
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
  uvIntensity = uvIntensity +(uvIntensity*.1);   //adding 10% because of plastic cover
  double index=uvIntensity * 1.61; 

  if(index<0)
    index=0;

  return index;
}

String ADCHandler::isWet(int adcReading)
{
  if(adcReading > 1000)
    return "wet";
  else
    return "dry";
}
