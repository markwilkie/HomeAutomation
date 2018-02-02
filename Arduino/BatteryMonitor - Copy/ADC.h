#include "FastRunningMedian.h"
#include <CircularBuffer.h>   //https://github.com/rlogiacco/CircularBuffer

class PrecADC {
private:
  //buffers for getting median on ADC reads
  FastRunningMedian<long,SAMPLE_SIZE, 0> adcBufferRaw;  //used by each read to store raw result (cleared each time)
  FastRunningMedian<long,BUFFER_SIZE, 0> adcBuffer0; //adc0 - mV from WCS1800, low amp panel
  
  //buffers for over time
  CircularBuffer<long> secondBuf0 = CircularBuffer<long>(60);  
  CircularBuffer<long> minuteBuf0 = CircularBuffer<long>(60);  
  CircularBuffer<long> hourBuf0  = CircularBuffer<long>(24);
  CircularBuffer<long> dayBuf0  = CircularBuffer<long>(30);
 
public:
 PrecADC(int value,int value, int value);  // adc number, sample size, buffer size
 read(); //reads according to sample size
 add();  //called every second
};
