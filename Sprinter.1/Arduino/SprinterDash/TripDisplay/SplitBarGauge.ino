#include "SplitBarGauge.h"

void SplitBarGauge::init(Genie *_geniePtr,int _lowObjNum,int _highObjNum,int _min,int _max,int _refreshTicks)
{
    geniePtr=_geniePtr;
    lowObjNum=_lowObjNum;   
    highObjNum=_highObjNum;

    min=_min;
    max=_max;
    refreshTicks=_refreshTicks;

    //init values
    nextTickCount=millis()+refreshTicks;
    currentValue=0;
    lastValue=0;    
}

void SplitBarGauge::init(Genie *_geniePtr,int _lowObjNum,int _highObjNum,int _digitsObjNum,int _min,int _max,int _refreshTicks)
{
    geniePtr=_geniePtr;
    lowObjNum=_lowObjNum;   
    highObjNum=_highObjNum;
    digitsObjNum=_digitsObjNum;

    min=_min;
    max=_max;
    refreshTicks=_refreshTicks;
}

int SplitBarGauge::getCurrentValue()
{
    return currentValue;
}

void SplitBarGauge::setValue(int _value)
{
    //Set actual value
    currentValue=_value;

    int midPoint=(max+min)/2;

    //Set value for the gauge
    if(currentValue>=midPoint)
        _value=_value-midPoint;
    if(currentValue<midPoint)
        _value=midPoint-_value;

    //Check bounds
    if(_value<0)
        _value=0;
    if(_value>(max-midPoint))
        _value=(max-midPoint);   

    //Set gauges
    if(currentValue>=midPoint)
    {
        currentHighValue=_value;
        currentLowValue=0;
    }
    if(currentValue<midPoint)
    {
        currentLowValue=_value;
        currentHighValue=0;
    }
}


void SplitBarGauge::update()
{  
    //Determine if it's time to update load value 
    int delta=lastValue-currentValue;
    if(millis()>=nextTickCount && delta!=0)
    {
        //Update timing
        nextTickCount=millis()+refreshTicks;
        lastValue=currentValue;

        //Update both objects 
        geniePtr->WriteObject(GENIE_OBJ_GAUGE, lowObjNum, currentLowValue);  
        geniePtr->WriteObject(GENIE_OBJ_GAUGE, highObjNum, currentHighValue);  

        //Write digits if appropriate
        if(digitsObjNum>=0)
            geniePtr->WriteObject(GENIE_OBJ_ILED_DIGITS, digitsObjNum, abs(currentValue)); 
    }
}