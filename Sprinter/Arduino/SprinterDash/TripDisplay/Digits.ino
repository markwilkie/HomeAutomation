#include "Digits.h"

Digits::Digits(Genie *_geniePtr,int _digitsObjNum,int _min,int _max,int _decimal,int _refreshTicks)
{
    geniePtr=_geniePtr;
    digitsObjNum=_digitsObjNum;

    min=_min;
    max=_max;
    decimal=_decimal;
    refreshTicks=_refreshTicks;
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

    //Serial.print("obj: ");
    //Serial.print(digitsObjNum);
    //Serial.print("  value: ");
    //Serial.print(currentValue);    
    //Serial.print("  digit: ");
    //Serial.println(currentDigitValue);    
}


void Digits::update(unsigned long currentTickCount)
{  

    //Determine if it's time to update load value (e.g. generate a new smoothing curve)
    double delta=abs(lastValue-currentValue);
    if((currentTickCount>=nextTickCount || currentTickCount==0) && delta>0.0)
    {
        //Update timing
        nextTickCount=currentTickCount+refreshTicks;
        lastValue=currentValue;

        geniePtr->WriteObject(GENIE_OBJ_ILED_DIGITS, digitsObjNum, currentDigitValue);  
    }
}