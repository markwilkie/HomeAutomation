#ifndef PrimaryForm_h
#define PrimaryForm_h

#include <genieArduino.h>

#include "TripSegmentData.h"

class PrimaryForm 
{
  public:
    PrimaryForm(Genie* geniePtr,int formID,const char* label);

    int getFormId();
    void displayForm();  //actually display form
    void updateData(int service,int pid,int value);
    void updateDisplay();

  private: 

    void updateGaugeData(int service,int pid,int value);

    //State
    int formID;
    const char* label;

    //Display
    Genie *geniePtr;       

    //Trip data
    TripSegmentData dataSinceLastStop=TripSegmentData();   
};

#endif
