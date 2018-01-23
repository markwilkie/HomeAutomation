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
    Serial.println("Adding to minute buffer: ");    
    minuteBuf.push(calcAvgFromBuffer(&secondBuf));       
    printStatus();

    if(minutes>59)
    {
      minutes=0;
      hours++;
      Serial.println("Adding to hour buffer: ");      
      hourBuf.push(calcAvgFromBuffer(&minuteBuf));    
      printStatus();      

      if(hours>23)
      {
        hours=0;
        Serial.println("Adding to day buffer: ");           
        dayBuf.push(calcAvgFromBuffer(&hourBuf)); 
        printStatus();          
      }
    }
  }

  //
  // TODO
  //
  // Add minutes, hours, etc if approprite using counters
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
  return (calcMilliAmps(calcAvgFromBuffer(&minuteBuf)));
}

long PrecADC::getLastDayAvg()
{
  return (calcMilliAmps(calcAvgFromBuffer(&hourBuf)));
}

long PrecADC::getLastMonthAvg()
{
  return (calcMilliAmps(calcAvgFromBuffer(&dayBuf)));
}

//
// private
//

long PrecADC::calcMilliAmps(long raw)
{
   return (((raw * gainFactor) - offset) / accuracy);  
}

long PrecADC::calcAvgFromBuffer(CircularBuffer<long> *circBuffer)
{
  long avg=0;
  for(int i=0;i<circBuffer->size();i++)
    avg=avg+(*circBuffer)[i];
    
  avg = avg/(circBuffer->size());

  return avg;
}

void PrecADC::printStatus()
{
  //print status
  Serial.println("======================");
  Serial.print("ADC# ") ; Serial.println(adcNum);
  Serial.print("mA last minute, hour, day, month avg: "); 
  Serial.print(getLastMinuteAvg());
  Serial.print(",");
  Serial.print(getLastHourAvg());
  Serial.print(",");
  Serial.print(getLastDayAvg());
  Serial.print(",");
  Serial.println(getLastMonthAvg());      
}

