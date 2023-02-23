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

    //State
    int formID;
    const char* label;

    //Display
    Genie *geniePtr;     
    TripData *tripSegDataPtr;  
};

#endif
