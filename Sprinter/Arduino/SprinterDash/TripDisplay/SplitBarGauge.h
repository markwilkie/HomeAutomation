#ifndef SPLITBARGAUGE_h
#define SPLITBARGAUGE_h

#include <genieArduino.h>

class SplitBarGauge 
{
  public:
    void init(Genie *_geniePtr,int _lowObjNum,int _highObjNum,int _min,int _max,int _refreshTicks);
    void init(Genie *_geniePtr,int _lowObjNum,int _highObjNum,int _digitsObjNum,int _min,int _max,int _refreshTicks);

    void setValue(int value);  //does not actually update the screen
    int getCurrentValue();
    void update();  //update actual digits on screen
    
  private: 
    Genie *geniePtr;

    //Data
    int currentValue;
    int lastValue;
    int currentHighValue;
    int currentLowValue;

    //Config
    int min=0;                  //Gauge minimum
    int max=100;                //Gauge maximum
    int decimal=0;              //Decimal
    int refreshTicks=1;         //How many ticks the gauge refreshes    
    int lowObjNum;              //Gauge for the numbers below mid point
    int highObjNum;
    int digitsObjNum=-1;         //Digits (zero if not using)

    //Timing
    unsigned long nextTickCount;      //when to update/refresh the gauge again
    long startTime;         //recorded when we update the gauge  (not when a new value comes across the wire)
};

#endif