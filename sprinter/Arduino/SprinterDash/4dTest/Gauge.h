#ifndef GAUGE_h
#define GAUGE_h

#include <genieArduino.h>

class Gauge 
{
  public:
    Gauge(Genie *_geniePtr,int _service,int _pid,int _range,int _angMeterObjNum,int _digitsObjNum);
    Gauge(Genie *_geniePtr,int _service,int _pid,int _normalizeRange,int _angMeterObjNum,int _digitsObjNum,int _deltaThreshold,int _refreshTicks,int _freqMs);

    //Pass each response in to see if there's a match
    bool isMatch(int incomingSvc, int incomingPid);

    void setValue(int value);
    void update(int currentTickCount);  //update actual gauge
    
  private: 

    //What this is  (e.g. rpm)
    int service;
    int pid;

    //Config
    float deltaThreshold=.05;   //.10 percent = .1...e.g., we'll just adjust end position and not reset curve if within 10%
    int refreshTicks=1;         //How many ticks the gauge refreshes
    int freqMs=200;             //Time smoothing happens over (unless threshold is crossed)
    int normalizedRange;        //Range for load so it's used to normalize     (e.g. 100 would be between 0-100) 

    //CAN values
    int currentValue;   //current value from CAN bus
    int lastValue;      //last reading from CAN bus to use to calc delta

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
    int nextTickCount;      //when to update/refresh the gauge again
    long startTime;         //recorded when we update the gauge  (not when a new value comes across the wire)
};

#endif