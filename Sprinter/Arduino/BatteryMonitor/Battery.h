#ifndef FASTRUNNING_H
#define FASTRUNNING_H
#include "FastRunningMedian.h"
#endif 

#ifndef BATTERY_H
#define BATTERY_H

#include "CircularBufferLib.h"   //https://github.com/luisllamasbinaburo/Arduino-CircularBuffer  

class Battery {
private:
  //Defines for 1x sok 100Ah LiFePO4 battery
  #define BAT_FULL 13330
  #define BAT_EMPTY 11200   //BMS shutoff is ~10.4 
  #define AH 100
  #define CHARGE_EFFICIENCY 90

  #define BAT_FLOAT 13800
  #define SOC_RESET 14230   
  #define BAT_CHARGE 14600

  //properties
  float stateOfCharge;
  long mAhRemaining;
  long socReset;
  long vMax;
  long vMaxTime;
  long vMin;
  long vMinTime;  
  long floatTime;
  long chargeTime;
  
  //Vars
  long vcc;
  float voltageCalibFactor;

  //Circular buffers
  CircularBuffer<long> mVMinBuf = CircularBuffer<long>(6);  //used to populate the 10th of an hour buffer
  CircularBuffer<long> mVTenthHourBuf = CircularBuffer<long>(20); //used to determine duty cycle for the last 2 hours
  CircularBuffer<int> mVGraphBuf = CircularBuffer<int>(32); //used to represent last 24 hours on the 16x2 char display 

  //Init onboard ADC buffer
  #define VOLTAGE_SAMPLE_SIZE 10

  //Members
  double calcSoCbyVoltage();
  long getMilliVolts();
  long calcAvgFromBuffer(CircularBuffer<long> *circBuffer,long prevBucketAvg);  
  long calcAvgFromBuffer(CircularBuffer<long> *circBuffer);  
  int getGraphEntry(long mv,bool resetFlag);
   
public:
   //members
   void begin(long vcc,long rtcNow); //starts stuff up and inits buffer
   void readThenAdd(long rtcNow); //reads according to sample size, then adds the result to the circular buffer
   double getVolts();
   void calibrateVoltage(float calibVolts);
   double getSoC() const { return stateOfCharge; }
   long getSoCReset() const { return socReset; }
   double getVMax() const { return vMax*.001; }
   double getVMin() const { return vMin*.001; } 
   long getFloatTime() const { return floatTime; } 
   long getChargeTime() const { return chargeTime; } 
   void resetSoC(long rtcNow,long currentmah);
   double getHoursRemaining(long mAflow); //calculates time remaining based on given flow
   double getAmpHoursRemaining(); //based on SoC 
   void updateSoC(long mAflow);
   double getDutyCycle();  
   long getmVGraphBuf(int i) const { return mVGraphBuf[i]; } ;
};

#endif 
