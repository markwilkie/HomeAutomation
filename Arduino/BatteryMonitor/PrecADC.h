#include <Adafruit_ADS1015.h>

#include "FastRunningMedian.h"
#include <CircularBuffer.h>   //https://github.com/rlogiacco/CircularBuffer

class PrecADC {
private:
  //ADC config
  #define SAMPLE_SIZE 5
  #define BUFFER_SIZE 10

  long calcMilliAmps(long raw);
  long calcAvgFromBuffer(CircularBuffer<long> *circBuffer);

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

  int seconds;
  int minutes;
  int hours;

  Adafruit_ADS1115 ads;
 
public:
 int adcNum;
 char *label;
 
 PrecADC(int n,int g,int o, float a, char* l); 
 void begin(); //starts stuff up and inits buffer
 void read(); //reads according to sample size and fills raw buffer, then adds the median to the main "raw" buffer
 void add();  //called every second and adds the median from the buffer to the seconds circular buffer 

 //return values in milli amps
 long getCurrent();  //grabs median from adcBuffer
 long getLastMinuteAvg();  //grabs average from secondBuf
 long getLastHourAvg();  //grabs average from minuteBuf
 long getLastDayAvg();  //grabs average from hourBuf
 long getLastMonthAvg(); //grabs average from dayBuf

 void calibrate();
 void printStatus();
};
