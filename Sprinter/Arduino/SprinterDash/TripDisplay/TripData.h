#ifndef TripData_h
#define TripData_h

#include "CurrentData.h"

#define FUEL_TANK_SIZE 22.0   // in gallons

class TripData
{
  public:
    TripData(CurrentData *currentDataPtr);

    void updateTripData();  //Call every loop so that calculations can be made where appropriate
    void resetTripData();   //Call when starting a new segment etc  (e.g. when ignition is turned off)

    int getMilesTravelled();
    double getHoursDriving();
    double getFuelGallonsUsed();
    double getInstantMPG();
    double getAvgMPG();
    int getMilesLeftInTank();
    int getCurrentElevation();
    int getTotalClimb();

  private: 
    CurrentData *currentDataPtr;
    bool startValuesSetFlag=false;

    //Particulars of this segment
    int startMiles=0;
    unsigned long startSeconds=0;
    int startFuelPerc=0;
    int totalClimb=0;
    int lastElevation=0;
};

#endif