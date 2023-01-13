#ifndef Trip_h
#define Trip_h

#include "TripSegment.h"
#include "Digits.h"
#include "SplitBarGauge.h"

#define TRIP_DISPLAY_SLOW_TICKS  10 //how often to update the display w/ trip info
#define TRIP_DISPLAY_FAST_TICKS  5  //how often to update fast

class Trip 
{
  public:
    Trip(Genie*);

    void update(int service,int pid,int value);
    void updateDisplay(unsigned long currentTickCount);
    void addTripSegment();

  private: 

    //Trip segments
    TripSegment* currentSegment=NULL;  //Current segment to use  (part of a linked list, so will be the last in the list)
    TripSegment* rootSegment=NULL;  //will always have a value as this is initialized in the constructor
    Genie *geniePtr;    

    //Digit objects
    Digits *avgMPG;
    Digits *milesLeftInTank;
    Digits *currentElevation;
    Digits *milesTravelled;
    Digits *hoursDriving;

    //Bar gauges
    SplitBarGauge *instMPG;
    SplitBarGauge *wind;
};

#endif