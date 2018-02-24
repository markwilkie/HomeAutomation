#ifndef FASTRUNNING_H
#define FASTRUNNING_H
#include "FastRunningMedian.h"
#endif 

#ifndef BATTERY_H
#define BATTERY_H

#include <CircularBuffer.h>   //https://github.com/rlogiacco/CircularBuffer

class Battery {
private:
  //Defines
  #define BAT_FULL 13000
  #define BAT_EMPTY 12400
  #define AH 310
  #define CHARGE_EFFICIENCY 94
  #define REST_CHARGE_LIMIT 1000
  #define REST_DRAIN_LIMIT 5000
  #define REST_WINDOW 72000  //20 hours

  #define BAT_FLOAT 13500
  #define BAT_CHARGE 14000
  
  //Vars
  long vcc;
  float stateOfCharge;
  long mAhRemaining;
  long socReset;

  int minutes;
  int tenthHours;

  //Circular buffers
  CircularBuffer<long> mVMinBuf = CircularBuffer<long>(6);  //used to populate the 10th of an hour buffer
  CircularBuffer<long> mVTenthHourBuf = CircularBuffer<long>(20); //used to determine duty cycle for the last 2 hours
  CircularBuffer<long> mVGraphBuf = CircularBuffer<long>(16); //used to represent last 24 hours on the 16 char display 

  //Init onboard ADC buffer
  #define VOLTAGE_SAMPLE_SIZE 10
  FastRunningMedian<long,VOLTAGE_SAMPLE_SIZE,0> adcBuffer;  

  //Members
  double calcSoCbyVoltage(long rtcNow,double temperature);
  long getMilliVolts();
  long calcAvgFromBuffer(CircularBuffer<long> *circBuffer,long prevBucketAvg);  
   
public:
 void begin(long vcc); //starts stuff up and inits buffer
 void readThenAdd(); //reads according to sample size, then adds the result to the circular buffer
 double getVolts();
 double getSoC();
 long getSoCReset();
 void adjustSoC(long rtcNow,double temperature,long socreset);
 double getHoursRemaining(long mAflow); //calculates time remaining based on given flow
 double getAmpHoursRemaining(); //based on SoC 
 void adjustAh(long mAflow);
 double getDutyCycle();  
};

#endif 
