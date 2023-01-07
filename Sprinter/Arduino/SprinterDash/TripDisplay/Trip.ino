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
    if(currentTickCount>=nextSlowTickCount)
    {
        nextFastTickCount=currentTickCount+TRIP_DISPLAY_FAST_TICKS;
        geniePtr->WriteObject(GENIE_OBJ_GAUGE, 1, currentSegment->getInstantMPG()-10);  //-10 is because the gauge starts at 10
    }

    //update slow counters?
    if(currentTickCount>=nextSlowTickCount)
    {
        nextSlowTickCount=currentTickCount+TRIP_DISPLAY_SLOW_TICKS;

        geniePtr->WriteObject(GENIE_OBJ_ILED_DIGITS, 6, currentSegment->getAvgMPG());
        geniePtr->WriteObject(GENIE_OBJ_ILED_DIGITS, 7, currentSegment->getMilesLeftInTank());
        geniePtr->WriteObject(GENIE_OBJ_ILED_DIGITS, 8, currentSegment->getCurrentElevation());
        geniePtr->WriteObject(GENIE_OBJ_ILED_DIGITS, 9, currentSegment->getMilesTravelled());
        geniePtr->WriteObject(GENIE_OBJ_ILED_DIGITS, 10, currentSegment->getHoursDriving());
    }
}

void Trip::addTripSegment()
{
    TripSegment *newSegment=new TripSegment();
    currentSegment->setNextSegment(newSegment);
    currentSegment=newSegment;
}