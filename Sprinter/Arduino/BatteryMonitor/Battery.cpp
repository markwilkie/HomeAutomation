#include "Battery.h"
#include "Arduino.h"
#include "debug.h"
#include "EEPROMAnything.h"

void Battery::begin(long _vcc,long rtcNow)
{
  //Set reference vcc
  vcc=_vcc;

  //Grab current voltage calibration factor
  EEPROM_readAnything(EEPROM_VOLTAGE_ADDR, voltageCalibFactor);
  Serial.print("EEPROM voltage factor read: "); Serial.println(voltageCalibFactor);  
  
  //Init time based buffers
  for(int i=0;i<6;i++) mVMinBuf.Add(-1L);
  for(int i=0;i<20;i++) mVTenthHourBuf.Add(-1L);
  for(int i=0;i<32;i++) mVGraphBuf.Add(-1L);  

  //Let's start with assuming full battery
  mAhRemaining=AH*1000L;  

  //Zero SoC
  stateOfCharge=calcSoCbyVoltage();
  socReset=rtcNow;  //used to know how long ago the SoC was last set

  //Zero last float time
  floatTime=-1;
  chargeTime=-1;

  //Set initial min/max
  vMax=getMilliVolts();
  vMin=getMilliVolts();
}

int Battery::getGraphEntry(long mv,bool resetFlag)
{
  // 0=uninitiated
  // 1=not
  // 2=float
  // 3=charging
  // 4=not to charging
  // 5=float to charging
  // 6=charging to float
  // 7=charging to not
  // 8=up/down
  // 9=multi up/down

  static int lastGraphState=0;
  static bool downFlag=false;
  static bool upFlag=false;
  int proposedGraphState=0;

  //not charging
  if(mv<BAT_FLOAT)
    proposedGraphState=1; //nothing

  //bulk
  if(mv>=BAT_FLOAT && mv<BAT_CHARGE)
    proposedGraphState=2; //float

  //charging
  if(mv>=BAT_CHARGE)
    proposedGraphState=3; //charge

  //Figure transitions
  if(proposedGraphState==1) //nothing
  {
    if(lastGraphState==3) //charge
    {
      downFlag=true;
      proposedGraphState=7; //charge to not
    }
  }
  if(proposedGraphState==2) //float
  {
    if(lastGraphState==3) //charge
    {
      downFlag=true;
      proposedGraphState=6; //charge to float
    }
  }  
  if(proposedGraphState==3) //charge
  {
    if(lastGraphState==2) //float
    {
      upFlag=true;
      proposedGraphState=5; //float to charge
    }
    if(lastGraphState==1) //not
    {
      upFlag=true;
      proposedGraphState=4; //not to charge
    }    
  } 
  if(upFlag && downFlag)
  {
    proposedGraphState=8; //up-down
  }
  if((upFlag || downFlag) && lastGraphState==8)
  {
    proposedGraphState=9; //multi up-down
  }

  //If flag, reset it
  if(resetFlag)
  {
    lastGraphState=0;
    downFlag=false;
    upFlag=false;
  }
  else
  {
    lastGraphState=proposedGraphState;
  }

  return proposedGraphState;
}

//Called every minute
void Battery::readThenAdd(long rtcNow)
{  
  static int minutes=0;
  static int graphMinutes=0;
  
  //Read current battery voltage
  long mv = getMilliVolts();

  //set min/max within a 24 hour window
  if(mv>vMax || ((rtcNow-vMaxTime) > (24L*60L*60L)))
  {
    vMaxTime=rtcNow;
    vMax=mv;
  }
  if(mv<vMin  || ((rtcNow-vMaxTime) > (24L*60L*60L)))
  {
    vMinTime=rtcNow;
    vMin=mv;
  }

  //set times for float and charge
  if(mv>=BAT_FLOAT && mv<BAT_CHARGE)
    floatTime=rtcNow;
  if(mv>=BAT_CHARGE)
    chargeTime=rtcNow;

  //Add current reading to CircularBufferlibs
  mVMinBuf.Add(mv);

  minutes++;
  if(minutes>=6)
  {
    minutes=0;
    mVTenthHourBuf.Add(calcAvgFromBuffer(&mVMinBuf,-1));     
  }

  //Graphing
  graphMinutes++;
  if(graphMinutes<45)
  {
    getGraphEntry(mv,false);
  }
  else
  {
    graphMinutes=0;
    int graphChar=getGraphEntry(mv,true);
    mVGraphBuf.Add(graphChar);     
  }
}

void Battery::calibrateVoltage(float calibVolts)
{
  //Read ADC
  long totRead=0;
  for(int i=0;i<VOLTAGE_SAMPLE_SIZE;i++)
  {
    totRead=totRead+analogRead(A0);
  }

  //calibrate new factor
  float factor=(calibVolts*1000)/(((totRead/VOLTAGE_SAMPLE_SIZE) * vcc) / 1024);

  //save to EEPROM
  Serial.print("Current factor: "); Serial.println(voltageCalibFactor);
  Serial.print("New factor: "); Serial.println(factor);

  //Save to offset EEPROM
  EEPROM_writeAnything(EEPROM_VOLTAGE_ADDR, factor);    
  voltageCalibFactor=factor;
}

long Battery::getMilliVolts()
{
  //Clear the ADC's throat
  analogRead(A0);
  
  //Read ADC
  long totRead=0;
  for(int i=0;i<VOLTAGE_SAMPLE_SIZE;i++)
  {
    totRead=totRead+analogRead(A0);
  }

  long mv=((totRead/VOLTAGE_SAMPLE_SIZE) * vcc) / 1024 ; // convert readings to mv    

  //voltage divider ratio = 5.137, r2/(r1+r2) where r1=10.33K and r2=2.497K
  //mv = mv * 4.95;  ;
  mv = mv * voltageCalibFactor;

  return mv;
}

double Battery::getVolts()
{
  return getMilliVolts()*.001;
}

void Battery::adjustSoC(long rtcNow,long drainmah)
{

  /*
   * - Set SoC to 100% when float + charge duty cycle <25%  (perhaps add last hour is float, and current is float)
     - Set SoC to 100% when SoC > 100% AND 1Ah is discharged
   */

  //If we've discharged more than 1Ah and SoC > 100, then we'll reset
  if(drainmah<-1000 && stateOfCharge > 100)
  {
    Serial.println("###  Reseting based on 1Ah");
    stateOfCharge=100;
    mAhRemaining=AH*1000L; 
    socReset=rtcNow;  //used to know how long ago the SoC was last set
    return;
  }

  //Let's get duty cycle and see if should reset SoC or not
  double dutyCycle=getDutyCycle();
  if(dutyCycle<=.25 && getMilliVolts() < BAT_CHARGE && getMilliVolts() >= BAT_FLOAT)
  {
    Serial.println("###  Reseting based on duty cycle");
    stateOfCharge=100;
    mAhRemaining=AH*1000L; 
    socReset=rtcNow;  //used to know how long ago the SoC was last set
    return;    
  } 
}

//Update the number of Ah remaining on the battery based on givecurrent flow
void Battery::adjustAh(long mAhFlow)
{    
    //If charge, add effeciency
    if(mAhFlow > 0)
      mAhFlow=mAhFlow*(CHARGE_EFFICIENCY * .01);
  
    //Ok, update amp hours - making sure we divide the flow by 60, as we'll do it 60 times in one hour
    mAhRemaining=mAhRemaining+(mAhFlow/60L);  

    //Now update SoC
    stateOfCharge=((mAhRemaining/1000.0)/AH)*100.0;
}

//Return % state of charge based on battery level
double Battery::calcSoCbyVoltage()
{ 
  //
  int full=BAT_FULL; 
  int empty=BAT_EMPTY;

  //Get voltage
  long mv=getMilliVolts(); 

  //Calc increments per 1%, then percentage
  double voltRange=full-empty;
  double soc=((mv-empty)/voltRange)*100.0;


  //set mahremaining based on SoC percentage
  mAhRemaining=(AH*(soc/100.0))*1000.0;

  return soc;
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

  //Normalize data
  if(hoursRemaining<0)
    hoursRemaining=0;
  if(hoursRemaining>99)
    hoursRemaining=99;

  return hoursRemaining;
}

double Battery::getDutyCycle()
{  
  //Figure dity cycle
  int chargeCount=0;
  for(int i=0;i<mVTenthHourBuf.Count();i++)
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
  double dutyCycle=chargeCount/mVTenthHourBuf.Count(); 
  return (dutyCycle);
}

long Battery::calcAvgFromBuffer(CircularBuffer<long> *circBuffer,long prevBucketAvg)
{
  long avg=0;
  int actualCount=0;
  for(int i=0;i<circBuffer->Count();i++)
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
