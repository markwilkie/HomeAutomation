#ifndef GAUGE_h
#define GAUGE_h

#include <genieArduino.h>

class Gauge 
{
  public:
    void init(Genie *_geniePtr,int _angMeterObjNum,int _digitsObjNum,int _min,int _max,int _refreshTicks);

    //Pass each response in to see if there's a match
    bool isMatch(int incomingSvc, int incomingPid);

    void setValue(int value);
    int getCurrentValue();
    void update();  //update actual gauge
    
  private: 

    //What this is  (e.g. rpm)
    int service;
    int pid;

    //Config
    int min=0;                  //Gauge minimum
    int max=100;                //Gauge maximum
    int refreshTicks=1;         //How many ticks the gauge refreshes    
    float deltaThreshold=.05;   //.10 percent = .1...e.g., we'll just adjust end position and not reset curve if within 10%

    //CAN values
    int currentValue;   //current value from CAN bus
    int lastValue;      //last reading from CAN bus to use to calc delta
    int gaugeValue;

    //Gauge variables
    //All values are normalized between 0-1
    Genie *geniePtr;
    int angMeterObjNum;     //Number of the target angular object on the form  (-1 means it's not used)
    int digitsObjNum;
    float currentGauge;     //current gauge reading 
    float normalizedDelta;  //delta from the last gauge reading
    float begPos;           //starting gauge position;
    float endPos;           //ending gauge position;

    //Timing
    unsigned long nextTickCount;      //when to update/refresh the gauge again
};

#endif