#include "PrecADCList.h"

PrecADCList::PrecADCList()
{
}

void PrecADCList::begin()
{
  for(int i=0;i<ADC_COUNT;i++)
    (*adcList)[i].begin();
}

void PrecADCList::read()
{
  for(int i=0;i<ADC_COUNT;i++)
    (*adcList)[i].read();
}

void PrecADCList::add()
{
  for(int i=0;i<ADC_COUNT;i++)
    (*adcList)[i].add();
}

PrecADC *PrecADCList::getADC(int adcNum)
{
   return &(*adcList)[adcNum];
}

long PrecADCList::getCurrent()
{
  long total=0;
  for(int i=0;i<ADC_COUNT;i++)
    total=total+(*adcList)[i].getCurrent();

  return total;
}


long PrecADCList::getLastMinuteAvg()
{
  long total=0;
  for(int i=0;i<ADC_COUNT;i++)
    total=total+(*adcList)[i].getLastMinuteAvg();

  return total;
}

long PrecADCList::getLastHourAvg()
{
  long total=0;
  for(int i=0;i<ADC_COUNT;i++)
    total=total+(*adcList)[i].getLastHourAvg();

  return total;
}

long PrecADCList::getLastDayAvg()
{
  long total=0;
  for(int i=0;i<ADC_COUNT;i++)
    total=total+(*adcList)[i].getLastDayAvg();

  return total;
}

long PrecADCList::getLastMonthAvg()
{
  long total=0;
  for(int i=0;i<ADC_COUNT;i++)
    total=total+(*adcList)[i].getLastMonthAvg();

  return total;
}

long PrecADCList::getCurrentCharge()
{
  long total=0;
  for(int i=0;i<ADC_COUNT;i++)
  {
    long value=(*adcList)[i].getCurrent();
    if(value>0)
      total=total+value;
  }

  return total;
}


long PrecADCList::getLastMinuteChargeAvg()
{
  long total=0;
  for(int i=0;i<ADC_COUNT;i++)
  {
    long value=(*adcList)[i].getLastMinuteAvg();
    if(value>0)
      total=total+value;
  }

  return total;
}

long PrecADCList::getLastHourChargeAvg()
{
  long total=0;
  for(int i=0;i<ADC_COUNT;i++)
  {
    long value=(*adcList)[i].getLastHourAvg();
    if(value>0)
      total=total+value;
  }

  return total;
}

long PrecADCList::getLastDayChargeAvg()
{
  long total=0;
  for(int i=0;i<ADC_COUNT;i++)
  {
    long value=(*adcList)[i].getLastDayAvg();
    if(value>0)
      total=total+value;
  }

  return total;
}

long PrecADCList::getLastMonthChargeAvg()
{
  long total=0;
  for(int i=0;i<ADC_COUNT;i++)
  {
    long value=(*adcList)[i].getLastMonthAvg();
    if(value>0)
      total=total+value;
  }

  return total;
}

void PrecADCList::calibrateADC(int adcNum)
{
  (*adcList)[adcNum].calibrate();
}

void PrecADCList::printStatus()
{
  Serial.println("============================");    
  Serial.println("ADC List:  (mA now, avg minute, avg hour, avg day, and avg month)");
  for(int i=0;i<ADC_COUNT;i++)
    (*adcList)[i].printStatus();
}

