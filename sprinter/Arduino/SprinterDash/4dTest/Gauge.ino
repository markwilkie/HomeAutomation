#include "Gauge.h"


Gauge::Gauge(Genie *_geniePtr,int _service,int _pid,int _normalizeRange,int _angMeterObjNum,int _digitsObjNum)
{
    geniePtr=_geniePtr;
    service=_service;
    pid=_pid;
    normalizedRange=_normalizeRange;
    angMeterObjNum=_angMeterObjNum;
    digitsObjNum=_digitsObjNum;
}

Gauge::Gauge(Genie *_geniePtr,int _service,int _pid,int _normalizeRange,int _angMeterObjNum,int _digitsObjNum,int _deltaThreshold,int _refreshTicks,int _freqMs)
{
    geniePtr=_geniePtr;
    service=_service;
    pid=_pid;
    normalizedRange=_normalizeRange;
    angMeterObjNum=_angMeterObjNum;
    digitsObjNum=_digitsObjNum;

    deltaThreshold=_deltaThreshold;
    refreshTicks=_refreshTicks;
    freqMs=_freqMs;
}


bool Gauge::isMatch(int incomingSvc, int incomingPid)
{
    if(incomingSvc==service && incomingPid==pid)
        return true;
    
    return false;
}

void Gauge::setValue(int _value)
{
    currentValue=_value;
}

/*
We're solving for a position at a given time, e.g. p(t)

t = time = where in time (x axis) I'm at
b = beginning = beginning position
c = change = how much it'll be changed at the end, or end pos - beg pos (velocity is c/d)
d = duration = how long we want to take to change

So for linear, (e.g. car travelling at a specific speed) it'd be:
t = 2 hours
position = 2 X velocity
p = 2 X (change / duration)    e.g. 120 miles traveled / 2 hours == 60

Of the 4 variables, only 1 changes....time.  So, we'll solve for position given time.

So for a gauge, the variables will corrospond to:

t = time = current millis()
b = beginning = gauge # (e.g. 0 for the load gauge)
c = change = e.g. end pos - beg pos.  This case let's say it's 40-0 (velocity is c/d)
d = duration = 500 * (c / range)   e.g. 500*(40/100) (500ms for the biggest swing possible?  Should be a configurable value)
 */

void Gauge::update(int currentTickCount)
{  
    //Determine if it's time to update load value (e.g. generate a new smoothing curve)
    int delta=abs(lastValue-currentValue);
    if((currentTickCount>=nextTickCount || currentTickCount==0) && delta>0)
    {
        //Update timing
        nextTickCount=currentTickCount+refreshTicks;
        lastValue=currentValue;

        //Figure what we want the gauge to be set to
        if(delta>currentValue*deltaThreshold)
        {   
            startTime=millis();
            begPos=currentGauge;
        }

        //Define where we want to end up
        endPos=(float)currentValue/(float)normalizedRange;   //normalize load to between 0-1    
        normalizedDelta=endPos-begPos;
    }

    //Update load smoothing using time
    long tms=millis()-startTime;
    if(tms<=freqMs)
    {
        float t=(float)tms/(float)freqMs;  //needs to be normalized between 0 and 1
        if (t < .5) {
            currentGauge = (normalizedDelta) * (t*t*t*4) + begPos;
        }
        else {
            float tt=(t*2)-2;
            currentGauge = (normalizedDelta) * ((tt*tt*tt*.5)+1) + begPos;
        }    

        //Update gauge  (gotta have this here because of the smoothing)
        if(angMeterObjNum>=0)
            geniePtr->WriteObject(GENIE_OBJ_IANGULAR_METER, angMeterObjNum, currentGauge*normalizedRange);
        if(digitsObjNum>=0)
            geniePtr->WriteObject(GENIE_OBJ_ILED_DIGITS, digitsObjNum, currentValue);       
    }    
}