#include "PrecADC.h"

PrecADC::PrecADC(int n,int g,int o, float a)
{
  adcNum=n;
  offset=o;
  gain=g;
  accuracy=a;

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
}

void PrecADC::add()
{   
  secondBuf.push(adcBuffer.getMedian());

  seconds++;
  if(seconds>59)
  {
    seconds=0;
    minutes++;
    minuteBuf.push(calcAvgFromBuffer(&secondBuf));       
    printStatus();
  }
   
  if(minutes>59)
  {
    minutes=0;
    hours++;
    hourBuf.push(calcAvgFromBuffer(&minuteBuf));     
  }

  if(hours>23)
  {
    hours=0;
    dayBuf.push(calcAvgFromBuffer(&hourBuf));        
  }
}

//grabs median from adcBuffer
long PrecADC::getCurrent()
{
  return (calcMilliAmps(adcBuffer.getMedian()));
}

long PrecADC::getLastMinuteAvg()
{
  return (calcMilliAmps(calcAvgFromBuffer(&secondBuf)));
}

long PrecADC::getLastHourAvg()
{ 
  long avg=calcAvgFromBuffer(&secondBuf);
  long avgMin=calcAvgFromBuffer(&minuteBuf);
  if(avgMin>0)
  {
    avg=avg+avgMin;
    avg=avg/2;
  }
  
  return (calcMilliAmps(avg));
}

long PrecADC::getLastDayAvg()
{  
  int denom=1;
  long avg=calcAvgFromBuffer(&secondBuf);
  long avgMin=calcAvgFromBuffer(&minuteBuf);
  if(avgMin>0)
  {
    denom++;
    avg=avg+avgMin;
    long avgHour=calcAvgFromBuffer(&hourBuf);
    if(avgHour>0)
    {
      denom++;
      avg=avg+avgHour;
    }
  }

  //calc average
  avg=avg/denom;
    
  return (calcMilliAmps(avg));
}

long PrecADC::getLastMonthAvg()
{
  int denom=1;
  long avg=calcAvgFromBuffer(&secondBuf);
  long avgMin=calcAvgFromBuffer(&minuteBuf);
  if(avgMin>0)
  {
    denom++;
    avg=avg+avgMin;
    long avgHour=calcAvgFromBuffer(&hourBuf);
    if(avgHour>0)
    {
      denom++;
      avg=avg+avgHour;
      long avgDay=calcAvgFromBuffer(&dayBuf);
      if(avgDay>0)
      {
        denom++;
        avg=avg+avgDay;
      }
    }
  }

  //calc average
  avg=avg/denom;
    
  return (calcMilliAmps(avg));
}

//
// private
//

long PrecADC::calcMilliAmps(long raw)
{
  if(raw!=0 && accuracy > 0)
    return (((raw * gainFactor) - offset) / accuracy);  
  else
    return 0;
}

long PrecADC::calcAvgFromBuffer(CircularBuffer<long> *circBuffer)
{
  long avg=0;
  int actualCount=0;
  for(int i=0;i<circBuffer->size();i++)
  {
    //Check for make sure it's an initialized value
    if((*circBuffer)[i]!=-1)
    {
      avg=avg+(*circBuffer)[i];
      actualCount++;
    }
  }

  if(actualCount>0)
    avg = avg/actualCount;

  return avg;
}

void PrecADC::printStatus()
{
  //print status
  Serial.print("ADC# ") ; Serial.print(adcNum); Serial.print(" ");
  Serial.print("mA last minute, hour, day, month avg: "); 
  Serial.print(getLastMinuteAvg());
  Serial.print(",");
  Serial.print(getLastHourAvg());
  Serial.print(",");
  Serial.print(getLastDayAvg());
  Serial.print(",");
  Serial.println(getLastMonthAvg());      
}

