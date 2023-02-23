#include "SummaryForm.h"

#define TITLE_STRING 12
#define DRIVING_TIME_STRING 17
#define ELASPED_TIME_STRING 18
#define TIME_STOPPED_STRING 19
#define NUM_STOPS_STRING 27
#define TIME_PARKED_STRING 3

#define MILES_STRING 13
#define GALLONS_STRING 21
#define AVG_MPG_STRING 23
#define AVG_MOV_SPEED_STRING 25
#define ELEVATION_GAIN_STRING 10

// first segment in a linked list  (each tripsegment has the next one)
SummaryForm::SummaryForm(Genie* _geniePtr,int _formID,const char* _label,TripData *_tripSegDataPtr)
{
    //Save main genie pointer
    geniePtr=_geniePtr;

    //Save context
    formID=_formID;
    label=_label;

    tripSegDataPtr=_tripSegDataPtr;
}

int SummaryForm::getFormId()
{
    return formID;
}

//Update display
void SummaryForm::updateDisplay()
{
  //Update title
  geniePtr->WriteStr(TITLE_STRING, label);

  //Update time data fields
  geniePtr->WriteStr(DRIVING_TIME_STRING, label);
  geniePtr->WriteStr(ELASPED_TIME_STRING, tripSegDataPtr->getHoursDriving());
  geniePtr->WriteStr(TIME_STOPPED_STRING, 0);
  geniePtr->WriteStr(NUM_STOPS_STRING, 0);
  geniePtr->WriteStr(TIME_PARKED_STRING, 0);

  //Update moving data fields
  geniePtr->WriteStr(MILES_STRING, tripSegDataPtr->getMilesTravelled());
  geniePtr->WriteStr(GALLONS_STRING, tripSegDataPtr->getFuelGallonsUsed());
  geniePtr->WriteStr(AVG_MPG_STRING, tripSegDataPtr->getAvgMPG());
  geniePtr->WriteStr(AVG_MOV_SPEED_STRING, 0);
  geniePtr->WriteStr(ELEVATION_GAIN_STRING, tripSegDataPtr->getTotalClimb());
}
