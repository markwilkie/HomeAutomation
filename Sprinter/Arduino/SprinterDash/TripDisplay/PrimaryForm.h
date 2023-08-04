#ifndef PrimaryForm_h
#define PrimaryForm_h

#include <genieArduino.h>

#include "Gauge.h"
#include "Digits.h"
#include "SplitBarGauge.h"
#include "Led.h"

#include "TripData.h"

class PrimaryForm 
{
  public:
    PrimaryForm(Genie* geniePtr,int formID,const char* label,TripData *_dataSinceLastStop,CurrentData *_currentDataPtr);

    int getFormId();
    void updateDisplay();

  private: 
    //Update data by calc values from current data
    void updateData();

    //State
    int formID;
    const char* label;

    //Display
    Genie *geniePtr;
    TripData *dataSinceLastStop;
    CurrentData *currentDataPtr;

    //Radial gauges
    Gauge loadGauge;
    Gauge boostGauge;
    Gauge coolantTempGauge; 
    Gauge transTempGauge;    

    //Split bar gauges
    SplitBarGauge windSpeedGauge;
    SplitBarGauge instMPG;

    //Create objects for the digit displays
    Digits avgMPG;
    Digits milesLeftInTank;
    Digits currentElevation;
    Digits milesTravelled;
    Digits hoursDriving;

    //LED objects
    Led codesLed;   
};

#endif
