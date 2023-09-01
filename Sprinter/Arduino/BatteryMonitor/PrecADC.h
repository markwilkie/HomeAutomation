#ifndef FASTRUNNING_H
#define FASTRUNNING_H
#include "FastRunningMedian.h"
#endif 

#ifndef PRECADC_H
#define PRECADC_H

#include <Adafruit_ADS1X15.h>
#include "CircularBufferLib.h"   //https://github.com/luisllamasbinaburo/Arduino-CircularBuffer

class PrecADC {
private:
  //ADC config
  #define SAMPLE_SIZE 5
  #define BUFFER_SIZE 10
  #define MAX_CALIBRATION 500

  long calcMilliAmps(long raw,int numOfSamples);
  int getBufferCount(CircularBuffer<long> *circBuffer);
  long calcAvgFromBuffer(CircularBuffer<long> *circBuffer,long prevBucketAvg);
  long calcSumFromBuffer(CircularBuffer<long> *circBuffer);
  long calcSumFromBuffer(CircularBuffer<long> *circBuffer,int size);

  long getCurrentRaw();  //grabs median from adcBuffer
  long getLastMinuteAvgRaw();  //grabs average from secondBuf
  long getLastHourAvgRaw();  //grabs average from minuteBuf
  long getLastDayAvgRaw();  //grabs average from hourBuf
  long getLastMonthAvgRaw(); //grabs average from dayBuf  

  //buffers for getting median on ADC reads
  FastRunningMedian<long,SAMPLE_SIZE, 0> adcReadBuffer;  //used by each read to store raw result 
  FastRunningMedian<long,BUFFER_SIZE, 0> adcBuffer; //added to each loop (approx 5 Hz right now)
  
  //buffers for over time
  CircularBuffer<long> secondBuf = CircularBuffer<long>(60);  
  CircularBuffer<long> minuteBuf = CircularBuffer<long>(60);  
  CircularBuffer<long> hourBuf  = CircularBuffer<long>(24);
  CircularBuffer<long> dayBuf  = CircularBuffer<long>(30);

  //vars
  int gain;
  float gainFactor;
  float accuracy;
  int offset;
  int offsetAdj;  //calibration value stored in EEPROM
  int sign;

  int seconds;
  int minutes;
  int hours;

  Adafruit_ADS1115 ads;
 
public:
 int adcNum;
 char *label;
 
 PrecADC(int n,int g,int o, float a, char* l, int sign); 
 void begin(); //starts stuff up and inits buffer
 void read(); //reads according to sample size and fills raw buffer, then adds the median to the main "raw" buffer
 void add();  //called every second and adds the median from the buffer to the seconds circular buffer 

 // get sum of the buffer for the last n seconds
 long getSum(long seconds);

 //return values in milli amps
 long getCurrent();  //grabs median from adcBuffer
 long getLastMinuteAvg();  //grabs average from secondBuf
 long getLastHourAvg();  //grabs average from minuteBuf
 long getLastDayAvg();  //grabs average from hourBuf
 long getLastMonthAvg(); //grabs average from dayBuf

 //return values in milli amps
 long getLastMinuteSum();  //grabs average from secondBuf
 long getLastHourSum();  //grabs average from minuteBuf
 long getLastDaySum();  //grabs average from hourBuf
 long getLastMonthSum(); //grabs average from dayBuf 

 void calibrate();
 void calibrate(long milliAmps);
 void printAvg();
 void printSum();
};

#endif 
