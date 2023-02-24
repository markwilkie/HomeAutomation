#ifndef TripData_h
#define TripData_h

#include <EEPROM.h>
#include "CurrentData.h"

#define FUEL_TANK_SIZE 22.0   // in gallons

struct TripDataStruct{
    //Distance
    uint16_t startMiles;  
    uint16_t lastMiles;
    uint16_t priorTotalMiles;

    //Timing
    uint32_t startSeconds;
    uint32_t ignOffSeconds;
    uint16_t totalParkedSeconds;
    uint16_t totalStoppedSeconds;
    uint16_t numberOfStops;

    //Consumption
    double_t startFuelPerc;
    double_t lastFuelPerc;
    double_t priorTotalGallonsUsed;

    //Elevation
    uint16_t totalClimb;
    uint16_t lastElevation;
};

class TripData
{
  public:
    TripData(CurrentData *currentDataPtr,int _tripIdx);

    void updateTripData();  //Call every loop so that calculations can be made where appropriate
    void resetTripData();   //Call when starting a new segment etc  (e.g. when ignition is turned off)
    void saveTripData();    //Saves data to EEPROM
    void loadTripData();
    void dumpTripData();

    int getMilesTravelled();
    double getDrivingTime();
    double getElapsedTime();
    double getStoppedTime();
    double getParkedTime();
    int getNumberOfStops();
    double getFuelGallonsUsed();
    double getInstantMPG();
    double getAvgMPG();
    double getAvgMovingSpeed();
    int getMilesLeftInTank();
    int getCurrentElevation();
    int getTotalClimb();

  private: 
    CurrentData *currentDataPtr;
    int tripIdx;

    TripDataStruct data;
};

#endif