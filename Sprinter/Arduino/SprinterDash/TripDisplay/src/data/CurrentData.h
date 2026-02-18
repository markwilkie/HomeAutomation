#ifndef CurrentData_h
#define CurrentData_h

#include "../sensors/Sensors.h"
#include "../sensors/Filter.h"

struct PIDStruct{
    //Distance
    int service;  
    int pid;
    char *label;
    bool online;
};

//PIDs we're looking for (defined in CurrentData.cpp)
extern PIDStruct load;
extern PIDStruct distance;
extern PIDStruct fuel;
extern PIDStruct maf;
extern PIDStruct speed;
extern PIDStruct coolant;
extern PIDStruct trans;
extern PIDStruct manPres;
extern PIDStruct diag;

extern PIDStruct* pidArray[];

class CurrentData
{
  public:
    CurrentData();
    void init();
    void setTime(unsigned long secondsToSet);
    double calibratePitot();
    void setPitotCalibrationFactor(double_t factor);
    int readPitot();
    bool verifyInterfaces(int service, int pid, int value);
    void updateStatusForm(char *listOfOfflineInterfaces);
    void updateDataFromPIDs(int service,int pid,int value);
    void updateDataFromSensors();
    void setBaroElevationOffset(int offset);  // set by ElevationAPI auto-calibration
    int  getRawBaroElevation();               // uncorrected reading for calibration
    int  getBaroElevationOffset();            // current offset
    void dumpData();

    //Data
    int currentMiles=0;
    bool currentMilesOnline=false;
    unsigned long currentSeconds=0;
    int currentFuelPerc=0;
    bool currentFuelPercOnline=false;
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
    bool idlingAsStoppedFlag=false;   // we've been idling for more than n time

    //GPS data (updated externally from GPSModule in main loop)
    float currentLatitude=0;
    float currentLongitude=0;
    int   currentSatellites=0;
    bool  gpsHasFix=false;

  private: 
    //Other sensors
    Pitot pitot;
    IgnState ignState;
    Barometer barometer;
    RTC rtc;
    LDR ldr;

    void initConnections();

    //Filter for outlier values
    Filter distanceFilter;
    Filter fuelFilter;
};

#endif
