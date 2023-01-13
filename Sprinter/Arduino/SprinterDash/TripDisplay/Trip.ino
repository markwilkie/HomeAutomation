#include "Trip.h"

// first segment in a linked list  (each tripsegment has the next one)
Trip::Trip(Genie *_geniePtr)
{
    //Save main genie pointer
    geniePtr=_geniePtr;

    //Create root trip segment
    rootSegment=new TripSegment();
    currentSegment=rootSegment;

    //Create objects for the digit displays
    avgMPG = new Digits(geniePtr,6,0,99,1,11);
    milesLeftInTank = new Digits(geniePtr,7,0,999,0,9);
    currentElevation = new Digits(geniePtr,8,0,9999,0,14);
    milesTravelled = new Digits(geniePtr,9,0,999,0,19);
    hoursDriving = new Digits(geniePtr,5,0,9,1,19);

    //Bar gauges
    instMPG = new SplitBarGauge(geniePtr,1,2,0,20,24);
    wind = new SplitBarGauge(geniePtr,3,0,0,60,26);  
}

//Update values, then update display
void Trip::update(int service,int pid,int value)
{
    //Update values in the current segment
    currentSegment->update(service,pid,value);

    //Update bar gauges
    instMPG->setValue(currentSegment->getInstantMPG());

    //Update digits
    avgMPG->setValue(currentSegment->getAvgMPG());
    milesLeftInTank->setValue(currentSegment->getMilesLeftInTank());
    currentElevation->setValue(currentSegment->getCurrentElevation());
    milesTravelled->setValue(currentSegment->getMilesTravelled());
    hoursDriving->setValue(currentSegment->getHoursDriving());    
}

//Update display
void Trip::updateDisplay(unsigned long currentTickCount)
{
    //Update bar gauges
    instMPG->update(currentTickCount);

    //Update digits
    avgMPG->update(currentTickCount);
    milesLeftInTank->update(currentTickCount);
    currentElevation->update(currentTickCount);
    milesTravelled->update(currentTickCount);
    hoursDriving->update(currentTickCount);
}

void Trip::addTripSegment()
{
    TripSegment *newSegment=new TripSegment();
    currentSegment->setNextSegment(newSegment);
    currentSegment=newSegment;
}