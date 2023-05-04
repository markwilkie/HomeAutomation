#include "Digits.h"

void Digits::init(Genie *_geniePtr,int _digitsObjNum,int _min,int _max,int _decimal,int _refreshTicks)
{
    geniePtr=_geniePtr;
    digitsObjNum=_digitsObjNum;

    min=_min;
    max=_max;
    decimal=_decimal;
    refreshTicks=_refreshTicks;

    //init values
    nextTickCount=millis()+refreshTicks;
    currentValue=0;
    lastValue=0;    
}

double Digits::getCurrentValue()
{
    return currentValue;
}

void Digits::setValue(double _value)
{
    //Set real value
    currentValue=_value;

    //make sure range is good
    _value=_value-min;
    if(_value<0)
        _value=0;
    if(_value>(max-min))
        _value=(max-min);    
        
    //format decimal
    if(decimal>0)
        _value=_value*(10*decimal);
    currentDigitValue=(int)_value;
}


void Digits::update()
{  

    //Determine if it's time to update load value (e.g. generate a new smoothing curve)
    double delta=abs(lastValue-currentValue);
    if(millis()>=nextTickCount && delta>0.0)
    {
        //Update timing
        nextTickCount=millis()+refreshTicks;
        lastValue=currentValue;

        geniePtr->WriteObject(GENIE_OBJ_ILED_DIGITS, digitsObjNum, currentDigitValue);  
    }
}