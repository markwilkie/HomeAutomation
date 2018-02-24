#include "Battery.h"
#include "Arduino.h"

void Battery::begin(long _vcc)
{
  //Set reference vcc
  vcc=_vcc;
  
  //Init time based buffers
  for(int i=0;i<6;i++) mVMinBuf.push(-1L);
  for(int i=0;i<20;i++) mVTenthHourBuf.push(-1L);
  for(int i=0;i<16;i++) mVGraphBuf.push(-1L);  

  //Let's start with assuming full battery
  mAhRemaining=AH*1000;  

  //Zero SoC
  stateOfCharge=-1;

  //Zero time tracking
  minutes=0;
  tenthHours=0;  
}

void Battery::readThenAdd()
{
  long mv = getMilliVolts();

  //Add to circularbuffers
  mVMinBuf.push(mv);

  minutes++;
  if(minutes>=6)
  {
    minutes=0;
    tenthHours++;
    mVTenthHourBuf.push(calcAvgFromBuffer(&mVMinBuf,-1));     
  }

  if(tenthHours>=20)
  {
    tenthHours=0;
    mVGraphBuf.push(calcAvgFromBuffer(&mVTenthHourBuf,-1));     
  }
}

long Battery::getMilliVolts()
{
  //Clear the ADC's throat
  analogRead(A0);
  
  //Read ADC
  adcBuffer.clear();
  for(int i=0;i<VOLTAGE_SAMPLE_SIZE;i++)
  {
    adcBuffer.addValue(analogRead(A0));
  }

  long mv=(adcBuffer.getMedian() * vcc) / 1024 ; // convert readings to mv    

  //voltage divider ratio = 5.137, r2/(r1+r2) where r1=10.33K and r2=2.497K
  mv = mv * 4.95;  

  return mv;
}

double Battery::getVolts()
{
  return getMilliVolts()*.001;
}

double Battery::getSoC()
{
  return stateOfCharge;
}

long Battery::getSoCReset()
{
  return socReset;
}

void Battery::adjustSoC(long rtcNow,double temperature,long drainmah)
{

  /*
   * - Set SoC to 100% when float + charge duty cycle <25%  (perhaps add last hour is float, and current is float)
     - Set SoC to 100% when SoC > 100% AND 1Ah is discharged
   */
   
  //If soc is invalid, set
  if(stateOfCharge<=0)
  {
     stateOfCharge = calcSoCbyVoltage(rtcNow,temperature);
     return;
  }

  //If we've discharged more than 1Ah and SoC > 100, then we'll reset
  if(drainmah<-1000 && stateOfCharge > 100)
  {
    stateOfCharge=100;
    return;
  }

  //Let's get duty cycle and see if should reset SoC or not
  double dutyCycle=getDutyCycle();
  if(dutyCycle<=.25 && mVMinBuf.last() < BAT_CHARGE)
  {
    stateOfCharge=100;
    return;    
  } 
}

//Return % state of charge based on battery level
double Battery::calcSoCbyVoltage(long rtcNow,double temperature)
{
  socReset=rtcNow;  //used to know how long ago the SoC was last set
  
  //@50deg F --> 12.4V = 50%, 12:54V = 60%, 12.68V = 70%, 12.82V = 80%, 12.96V = 90%, 13.1V = 100%
  //         -->              12.52V = 60%, 12.64V = 70%, 12.76V = 80%, 12.88V = 90%, 13V = 100%
  //
  // 3.33mv adjust for each degree F temperature change  (lower temp means higher voltage for full)
  //
  int full=BAT_FULL; //@50deg F
  int empty=BAT_EMPTY;

  //Adjust for temperature
  int offset=(50-temperature)*3.33;
  full=full+offset;
  empty=empty+offset;

  //Get voltage
  long mv=mVMinBuf.last(); //Get the last minute voltage 

  //Calc increments per 1%, then percentage
  double incr=(full-empty)/50.0;  //gives voltage per 1% in mV
  double sos=(((mv-empty)/incr) + 50);

  if(sos<0)
    sos=0;

  return sos;
}

double Battery::getAmpHoursRemaining()
{
  return mAhRemaining * .001;
}

//Calc's how long the battery will last to 50% based on flow in mA passed in
//Use this to calc time left based on any of the inputs, hour, day, etc....
double Battery::getHoursRemaining(long mAflow)
{ 
  //We should now be able to calc hours left.  If mAflow positive, return 999 if there are no amps
  double hoursRemaining;
  if(mAflow>=0)
  {
    long full=(((long)AH/2)*1000);
    hoursRemaining=(((double)full-mAhRemaining)/mAflow);  //hours to full charge
  }
  else
  {
    hoursRemaining=(mAhRemaining/(mAflow*-1.0)); //hours to 50% charge
  }

  return hoursRemaining;
}

//Update the number of Ah remaining on the battery based on givecurrent flow
void Battery::adjustAh(long mAhFlow)
{ 
    //If charge, add effeciency
    if(mAhFlow > 0)
      mAhFlow=mAhFlow*(CHARGE_EFFICIENCY * .01);
  
    //Ok, update amp hours - making sure we divide the flow by 60, as we'll do it 60 times in one hour
    mAhRemaining=mAhRemaining+(mAhFlow/60);  

    //Now update SoC
    stateOfCharge=(mAhRemaining/1000)/AH;
}

double Battery::getDutyCycle()
{  
  //Figure dity cycle
  int chargeCount=0;
  for(int i=0;i<mVTenthHourBuf.size();i++)
  {
    //Check for make sure it's an initialized value
    if(mVTenthHourBuf[i]!=-1)
    {
      //Count which are chargines and which are not
      if(mVTenthHourBuf[i]>=BAT_CHARGE)
      {
        chargeCount++;
      }
    }
  } 

  //Calc duty cycle
  double dutyCycle=chargeCount/mVTenthHourBuf.size(); 
  return (dutyCycle);
}

long Battery::calcAvgFromBuffer(CircularBuffer<long> *circBuffer,long prevBucketAvg)
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

