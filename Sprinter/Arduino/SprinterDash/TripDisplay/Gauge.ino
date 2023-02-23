#include "Gauge.h"

void Gauge::init(Genie *_geniePtr,int _angMeterObjNum,int _digitsObjNum,int _min,int _max,int _refreshTicks)
{
    geniePtr=_geniePtr;
    angMeterObjNum=_angMeterObjNum;
    digitsObjNum=_digitsObjNum;

    min=_min;
    max=_max;
    refreshTicks=_refreshTicks;

    //init values
    nextTickCount=millis()+refreshTicks;
    currentValue=0;
    lastValue=0;
}

int Gauge::getCurrentValue()
{
    return currentValue;
}

void Gauge::setValue(int _value)
{
    currentValue=_value;

    _value=_value-min;
    if(_value<0)
        _value=0;
    if(_value>(max-min))
        _value=(max-min);    
        
    gaugeValue=_value;
}

void Gauge::update()
{  
    //Determine if it's time to update value
    int delta=abs(lastValue-currentValue);
    if(millis()>=nextTickCount && delta>0)
    {
        //Update timing
        nextTickCount=millis()+refreshTicks;
        lastValue=currentValue;        

        if(angMeterObjNum>=0)
        {
            geniePtr->WriteObject(GENIE_OBJ_IANGULAR_METER, angMeterObjNum, gaugeValue);  
        }
        if(digitsObjNum>=0)
        {
            geniePtr->WriteObject(GENIE_OBJ_ILED_DIGITS, digitsObjNum, currentValue);   
        }
    }
}
