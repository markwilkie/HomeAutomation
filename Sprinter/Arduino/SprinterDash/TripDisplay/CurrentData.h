#ifndef CurrentData_h
#define CurrentData_h

#include "Pitot.h"
#include "IgnState.h"
#include "Barometer.h"
#include "RTC.h"

class CurrentData
{
  public:
    CurrentData();
    void init();
    void updateData(int service,int pid,int value);

    //Data
    int currentMiles=0;
    unsigned long currentSeconds=0;
    int currentFuelPerc=0;
    int currentElevation=0;
    int currentMAF=0;
    int currentSpeed=0;
    int currentPitotSpeed=0;
    int currentLoad=0;
    int currentCoolantTemp=0;
    int currentTransmissionTemp=0;
    int currentBoost=0;
    int currentLightLevel=0;
    bool codesPresent=false;
    bool ignitionState=false;

  private: 

    //Other sensors
    Pitot pitot;
    IgnState ignState;
    Barometer barometer;
    RTC rtc;

    void updateDataFromPIDs(int service,int pid,int value);
    void updateDataFromSensors();
};

#endif