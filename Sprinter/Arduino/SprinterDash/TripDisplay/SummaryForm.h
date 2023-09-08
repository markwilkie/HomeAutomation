#ifndef SummaryForm_h
#define SummaryForm_h

#include "FormHelpers.h"
#include <genieArduino.h>

#include "TripData.h"

class SummaryForm 
{
  public:
    SummaryForm(Genie* geniePtr,int formID,const char* label,TripData *tripSegDataPtr);

    int getFormId();
    void updateDisplay();

  private: 
    StrField strField;

    //State
    int formID;
    const char* label;

    //Display
    Genie *geniePtr;     
    TripData *tripSegDataPtr;  

    //Fields
    char title[15];
    char drivingTime[5];
    char elapsedTime[5];
    char stoppedTime[5];
    char numberOfStops[3];
    char parkedTime[5];
    char milesTravelled[5];
    char gallonsUsed[4];
    char avgMPG[5];
    char avgMovingSpeed[5];
    char elevationGain[6]; 

    const char* errorStr = "NA";
};

#endif
