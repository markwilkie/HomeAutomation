#include "Led.h"

void Led::init(Genie *_geniePtr,int _ledObjNum,int _refreshTicks)
{
    geniePtr=_geniePtr;
    ledObjNum=_ledObjNum;

    refreshTicks=_refreshTicks;

    //init values
    nextTickCount=millis()+refreshTicks;  
}

bool Led::isActive()
{
    return ledState|blink;
}

void Led::turnOn()
{
    ledState=true;
    blink=false;
}

void Led::turnOff()
{
    ledState=false;
    blink=false;
}

void Led::startBlink()
{
    ledState=false;
    blink=true;
}


void Led::update()
{  
    //Determine if it's time to update load value (e.g. generate a new smoothing curve)
    if(millis()>=nextTickCount && (blink || ledState!=lastState))
    {
        //Set last state
        lastState=ledState;

        //Update timing
        nextTickCount=millis()+refreshTicks;

        //determine state that should be shown
        if(blink)
        {
            if(ledState) {ledState=false;}
            else {ledState=true;}
        }
        
        //update led on form
        geniePtr->WriteObject(GENIE_OBJ_USER_LED, ledObjNum, ledState);  
    }
}