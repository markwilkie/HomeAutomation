#include "PrecADC.h"
#include "EEPROMAnything.h"

PrecADC::PrecADC(int n,int g,int o, float a, char *l, int s)
{
  adcNum=n;
  offset=o;
  gain=g;
  accuracy=a;
  label=l;
  sign=s;

  //Figure accuracy from gain
  if(gain==GAIN_ONE)
    gainFactor=.125;
  else if(gain==GAIN_TWO)
    gainFactor=.0625;
  else if(gain==GAIN_FOUR)
    gainFactor=.03125;
  else if(gain==GAIN_EIGHT)
    gainFactor=.015625;
  else if(gain==GAIN_SIXTEEN)
    gainFactor=.0078125;
  else 
    gainFactor=.1875; //default

  // ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit = 0.1875mV (default)
  // ads.setGain(GAIN_ONE);        // 1x gain   +/- 4.096V  1 bit = 0.125mV
  // ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit = 0.0625mV
  // ads.setGain(GAIN_FOUR);       // 4x gain   +/- 1.024V  1 bit = 0.03125mV
  // ads.setGain(GAIN_EIGHT);      // 8x gain   +/- 0.512V  1 bit = 0.015625mV
  // ads.setGain(GAIN_SIXTEEN);    // 16x gain  +/- 0.256V  1 bit = 0.0078125mV  
}

void PrecADC::begin()
{
  //Start ADC
  ads.setGain(gain);  
  ads.begin();  

  //Load calibration values from EEPROM 
  EEPROM_readAnything(EEPROM_PRECADC_ADDR+(adcNum*sizeof(offsetAdj)), offsetAdj);  
  Serial.print("EEPROM read for: "); Serial.print(adcNum); Serial.print("  value: "); Serial.println(offsetAdj); 
  if (offsetAdj >= (MAX_CALIBRATION*-1)  && offsetAdj <= MAX_CALIBRATION)
  {
     //yup, we've got good values here, let's adjust the offset
     offset=offset+offsetAdj;
     Serial.print("New calibrated offset: "); Serial.println(offset);
  }
  else
    offsetAdj=0;  

  //Init time based buffers
  for(int i=0;i<60;i++) secondBuf.push(-1L);
  for(int i=0;i<60;i++) minuteBuf.push(-1L);
  for(int i=0;i<24;i++) hourBuf.push(-1L);
  for(int i=0;i<30;i++) dayBuf.push(-1L);

  //Zero time tracking
  seconds=0;
  minutes=0;
  hours=0;  
}

void PrecADC::read()
{
  //Set gain
  ads.setGain(gain);  

  //Read for sample
  for(int i=0;i<SAMPLE_SIZE;i++)
  {
    adcReadBuffer.addValue(ads.readADC_SingleEnded(adcNum));
  }

  //Add median from samples to buffer
  adcBuffer.addValue(adcReadBuffer.getMedian());

  //Put adc back to default
  ads.setGain(GAIN_TWOTHIRDS);
}

void PrecADC::add()
{   
  secondBuf.push(adcBuffer.getMedian());

  seconds++;
  if(seconds>=59)  //since 59 is the 60th second, and we add the last (59th) to the second queue before promoting, we need to reset ON the 59th instead of 60th
  {
    seconds=0;
    minutes++;
    minuteBuf.push(calcAvgFromBuffer(&secondBuf,-1));       
  }
   
  if(minutes>=59)
  {
    minutes=0;
    hours++;
    hourBuf.push(calcAvgFromBuffer(&minuteBuf,-1));     
  }

  if(hours>=23)
  {
    hours=0;
    dayBuf.push(calcAvgFromBuffer(&hourBuf,-1));        
  }
}

//grabs median from adcBuffer
long PrecADC::getCurrentRaw()
{
  return adcBuffer.getMedian();
}

long PrecADC::getCurrent()
{
  return (calcMilliAmps(getCurrentRaw(),1));
}

long PrecADC::getLastMinuteAvgRaw()
{
  return calcAvgFromBuffer(&secondBuf,adcBuffer.getMedian());
}

long PrecADC::getLastMinuteAvg()
{
  return (calcMilliAmps(getLastMinuteAvgRaw(),1));
}

long PrecADC::getLastHourAvgRaw()
{ 
  long avg=getLastMinuteAvgRaw();
  long avgMin=calcAvgFromBuffer(&minuteBuf,avg);
  
  return (avgMin);
}

long PrecADC::getLastHourAvg()
{ 
  return (calcMilliAmps(getLastHourAvgRaw(),1));
}

long PrecADC::getLastDayAvgRaw()
{  
  long avg=getLastHourAvgRaw();
  long avgHour=calcAvgFromBuffer(&hourBuf,avg);
    
  return (avgHour);
}

long PrecADC::getLastDayAvg()
{  
  return (calcMilliAmps(getLastDayAvgRaw(),1));
}

long PrecADC::getLastMonthAvgRaw()
{
  long avg=getLastDayAvgRaw();
  long avgDay=calcAvgFromBuffer(&dayBuf,avg);
    
  return (avgDay);
}

long PrecADC::getLastMonthAvg()
{
  return (calcMilliAmps(getLastMonthAvgRaw(),1));
}

//==================

long PrecADC::getLastMinuteSum()
{
  int numOfSamples=getBufferCount(&secondBuf);
  long sum=calcSumFromBuffer(&secondBuf);  
  long ma=calcMilliAmps(sum,numOfSamples);
  ma=ma/(60L*60L); //convert to mAh from mA seconds
  
  return (ma);
}

long PrecADC::getLastHourSum()
{ 
  int numOfSamples=getBufferCount(&minuteBuf);
  long sumMin=calcSumFromBuffer(&minuteBuf);  
  long ma=calcMilliAmps(sumMin,numOfSamples);
  ma=ma/60L;  //convert to mAh from mA minutes

  return (ma+getLastMinuteSum());
}

long PrecADC::getLastDaySum()
{  
  int numOfSamples=getBufferCount(&hourBuf);
  long sumHour=calcSumFromBuffer(&hourBuf);  
  long ma=calcMilliAmps(sumHour,numOfSamples);
  
  return (ma+getLastHourSum());
}

long PrecADC::getLastMonthSum()
{
  int numOfSamples=getBufferCount(&dayBuf);
  long sumDay=calcSumFromBuffer(&dayBuf);
  long ma=calcMilliAmps(sumDay,numOfSamples);
  ma=ma*24L;  //convert to mAh from mA days
  
  return (ma+getLastDaySum());
}

//Assumes zero (resting) ADC
void PrecADC::calibrate()
{
  Serial.print("Current offset: "); Serial.println(offset);
  long mv=(adcBuffer.getMedian() * gainFactor);
  offsetAdj=mv-offset;
  offset=offset+offsetAdj;
  Serial.print("New offset: "); Serial.println(offset);

  //Save to offset EEPROM
  EEPROM_writeAnything(EEPROM_PRECADC_ADDR+(adcNum*sizeof(offsetAdj)), offsetAdj);   
}

//
// private
//

long PrecADC::calcMilliAmps(long raw,int numOfSamples)
{
  //0.1875mV default gain factor
  
  if(raw!=0 && accuracy > 0)
  {
    long newOffset=(long)offset * (long)numOfSamples;
    return ((((raw * gainFactor) - newOffset) / accuracy) * sign);  
  }
  else
    return 0;
}

int PrecADC::getBufferCount(CircularBuffer<long> *circBuffer)
{
  int actualCount=0;
  for(int i=0;i<circBuffer->size();i++)
  {
    //Check for make sure it's an initialized value
    if((*circBuffer)[i]!=-1)
    {
      actualCount++;
    }
  }

  return actualCount;
}

long PrecADC::calcAvgFromBuffer(CircularBuffer<long> *circBuffer,long prevBucketAvg)
{
  long avg=0;
  int actualCount=0;
  for(int i=0;i<circBuffer->size();i++)
  {
    //Check for make sure it's an initialized value
    if((*circBuffer)[i]!=-1)
    {
      //Serial.print("b:");Serial.println((*circBuffer)[i]);
      avg=avg+(*circBuffer)[i];
      actualCount++;
    }
  }

  //Add in previous bucket
  if(prevBucketAvg!=-1)
  {
    //Serial.print("a:");Serial.println(prevBucketAvg);
    avg=avg+prevBucketAvg;
    actualCount++;
  }

  if(actualCount>0)
    avg = avg/actualCount;

  //Serial.print("ret:");Serial.println(avg);

  return avg;
}

long PrecADC::calcSumFromBuffer(CircularBuffer<long> *circBuffer)
{
  long sum=0;
  for(int i=0;i<circBuffer->size();i++)
  {
    //Check for make sure it's an initialized value
    if((*circBuffer)[i]!=-1)
    {
      //Serial.print("b:");Serial.println((*circBuffer)[i]);
      sum=sum+((*circBuffer)[i]);
    }
  }

  //Serial.print("==> sum:");Serial.println(sum);

  return sum;
}

void PrecADC::printAvg()
{
  //print status
  Serial.print("ADC#") ; Serial.print(adcNum); Serial.print(" - "); Serial.print(label); Serial.print(" ");    
  Serial.print(getCurrent());
  Serial.print(",");
  Serial.print(getLastMinuteAvg());
  Serial.print(",");
  Serial.print(getLastHourAvg());
  Serial.print(",");
  Serial.print(getLastDayAvg());
  Serial.print(",");
  Serial.println(getLastMonthAvg());      
}

void PrecADC::printSum()
{
  //print status
  Serial.print("ADC#") ; Serial.print(adcNum); Serial.print(" - "); Serial.print(label); Serial.print(" ");    
  Serial.print(getLastMinuteSum());
  Serial.print(",");
  Serial.print(getLastHourSum());
  Serial.print(",");
  Serial.print(getLastDaySum());
  Serial.print(",");
  Serial.println(getLastMonthSum());      
}

