#ifndef SummaryForm_h
#define SummaryForm_h

#include <genieArduino.h>

#include "TripData.h"

class SummaryForm 
{
  public:
    SummaryForm(Genie* geniePtr,int formID,const char* label,TripData *tripSegDataPtr);

    int getFormId();
    void updateDisplay();

  private: 

    void updateField(int objNum,char *title,double value);
    void updateField(int objNum,char *title,double value,int fieldLen);

    //State
    int formID;
    const char* label;

    //Display
    Genie *geniePtr;     
    TripData *tripSegDataPtr;  

    //Fields
    char title[15];
    char drivingTime[4];
    char elapsedTime[4];
    char stoppedTime[4];
    char numberOfStops[2];
    char parkedTime[4];
    char milesTravelled[4];
    char gallonsUsed[3];
    char avgMPG[4];
    char avgMovingSpeed[4];
    char elevationGain[5]; 

    const char* errorStr = "NA";
};

#endif
