#ifndef CurrentData_h
#define CurrentData_h

#include "Sensors.h"
#include "Filter.h"

struct PIDStruct{
    //Distance
    int service;  
    int pid;
    char *label;
    bool online;
};

//PIDs we're looking for
PIDStruct load = { .service=0x41, .pid=0x04, .label="Load\n", .online=false }; 
PIDStruct distance = { .service=0x41, .pid=0x31, .label="Distance\n", .online=false }; 
PIDStruct fuel = { .service=0x41, .pid=0x2F, .label="Fuel\n", .online=false }; 
PIDStruct maf = { .service=0x41, .pid=0x10, .label="MAF\n", .online=false }; 
PIDStruct speed = { .service=0x41, .pid=0x0D, .label="Speed\n", .online=false }; 
PIDStruct coolant = { .service=0x41, .pid=0x05, .label="Coolant\n", .online=false }; 
PIDStruct trans = { .service=0x61, .pid=0x30, .label="Trans\n", .online=false }; 
PIDStruct manPres = { .service=0x41, .pid=0x0B, .label="Man Pres\n", .online=false }; 
PIDStruct diag = { .service=0x41, .pid=0x01, .label="Diag\n", .online=false }; 

PIDStruct* pidArray[]={&load,&distance,&fuel,&maf,&speed,&coolant,&trans,&manPres,&diag};

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