#ifndef TripData_h
#define TripData_h

#include <EEPROM.h>
#include "CurrentData.h"

#define FUEL_TANK_SIZE 24.5   // in gallons
#define FUEL_ADDITIVE_RATIO .4   // oz per gallons

struct TripDataStruct{
    //Distance
    uint32_t startMiles;  
    uint32_t lastMiles;
    uint32_t priorTotalMiles;

    //Timing
    uint32_t startSeconds;
    uint32_t ignOffSeconds;
    uint32_t totalParkedSeconds;
    uint32_t totalStoppedSeconds;
    uint16_t numberOfStops;

    //Consumption
    uint16_t stoppedFuelPerc;  //used to calc heater fueld consumption and/or fillups
    uint16_t startFuelPerc;
    float priorTotalGallonsUsed;  //this plus current tank = total gallons used

    //Elevation
    uint32_t totalClimb;
    uint16_t lastElevation;
};

class TripData
{
  public:
    TripData(CurrentData *currentDataPtr,PropBag *_propBagPtr,int _tripIdx);

    void ignitionOff(); 
    void ignitionOn();
    void updateElevation();  //Call every loop 
    void updateFuelGallonsUsed(); //Called by getGallonsUsed, and on startup/shutdown
    void resetTripData();   //Call when starting a new segment etc  (e.g. when ignition is turned off)
    void saveTripData(int offset);    //Saves data to EEPROM
    void loadTripData(int offset);
    void dumpTripData();

    int getMilesTravelled();
    double getDrivingTime();
    double getElapsedTime();
    double getStoppedTime();
    double getParkedTime();
    int getNumberOfStops();
    double getFuelGallonsUsed();
    double getHeaterGallonsUsed();
    double getGallonsExpected();
    double getInstantMPG();
    double getAvgMPG();
    double getAvgMovingSpeed();
    int getMilesLeftInTank();
    int getCurrentElevation();
    long getTotalClimb();

  private: 
    CurrentData *currentDataPtr;
    PropBag *propBagPtr;
    int tripIdx;

    TripDataStruct data;

    double fuelGallonsUsed;   //fuel gallons used

    //Used to keep a running average if instance mpg so we can calibrate on the fly
    unsigned long sumInstMPG=0;
    long numInstMPGSamples=0;
    double instMPG=0;
    double lastAvgInstMPG=0;
};

#endif
