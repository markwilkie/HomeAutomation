#include "Trip.h"

// first segment in a linked list  (each tripsegment has the next one)
Trip::Trip(Genie *_geniePtr)
{
    geniePtr=_geniePtr;
    rootSegment=new TripSegment();
    currentSegment=rootSegment;
}

//Update values, then update display
void Trip::update(int service,int pid,int value)
{
    //Update values in the current segment
    currentSegment->update(service,pid,value);
}

//Update display
void Trip::updateDisplay(unsigned long currentTickCount)
{
    //update fast gauges?
    if(currentTickCount>=nextFastTickCount)
    {
        nextFastTickCount=currentTickCount+TRIP_DISPLAY_FAST_TICKS;

        //instant MPG
        int instMPG=currentSegment->getInstantMPG();
        instMPG=instMPG-10;
        if(instMPG>(30-10))
            instMPG=20;
        if(instMPG<10)
            instMPG=0;
        instMPG=(30-10)-instMPG; //invert because the gauge is upside down
        geniePtr->WriteObject(GENIE_OBJ_GAUGE, 1, instMPG); 
    }

    //update slow counters?
    if(currentTickCount>=nextSlowTickCount)
    {
        nextSlowTickCount=currentTickCount+TRIP_DISPLAY_SLOW_TICKS;

        geniePtr->WriteObject(GENIE_OBJ_ILED_DIGITS, 6, currentSegment->getAvgMPG());
        geniePtr->WriteObject(GENIE_OBJ_ILED_DIGITS, 7, currentSegment->getMilesLeftInTank());
        geniePtr->WriteObject(GENIE_OBJ_ILED_DIGITS, 8, currentSegment->getCurrentElevation());
        geniePtr->WriteObject(GENIE_OBJ_ILED_DIGITS, 9, currentSegment->getMilesTravelled());
        geniePtr->WriteObject(GENIE_OBJ_ILED_DIGITS, 5, currentSegment->getHoursDriving());
    }
}

void Trip::addTripSegment()
{
    TripSegment *newSegment=new TripSegment();
    currentSegment->setNextSegment(newSegment);
    currentSegment=newSegment;
}