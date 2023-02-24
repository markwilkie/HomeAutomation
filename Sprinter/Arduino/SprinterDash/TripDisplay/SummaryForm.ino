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

//Field masks for sprintfs
char dblFormat[]={'%','x','.','1','l','f','\0'};
char decFormat[]={'%','x','d','\0'};

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
  updateField(DRIVING_TIME_STRING, drivingTime, tripSegDataPtr->getDrivingTime());
  updateField(ELASPED_TIME_STRING, elapsedTime, tripSegDataPtr->getElapsedTime());
  updateField(TIME_STOPPED_STRING, stoppedTime,tripSegDataPtr->getStoppedTime());
  updateField(NUM_STOPS_STRING, numberOfStops, tripSegDataPtr->getNumberOfStops());
  updateField(TIME_PARKED_STRING, parkedTime, tripSegDataPtr->getParkedTime());

  //Update moving data fields
  updateField(MILES_STRING, milesTravelled, tripSegDataPtr->getMilesTravelled());
  updateField(GALLONS_STRING, gallonsUsed, tripSegDataPtr->getFuelGallonsUsed());
  updateField(AVG_MPG_STRING, avgMPG, tripSegDataPtr->getAvgMPG());
  updateField(AVG_MOV_SPEED_STRING, avgMovingSpeed, tripSegDataPtr->getAvgMovingSpeed());
  updateField(ELEVATION_GAIN_STRING, elevationGain, tripSegDataPtr->getTotalClimb());
}

void SummaryForm::updateField(int objNum,char *field,double value)
{
  int fieldLen = sizeof(field);

  //Value in range?
  if(value<0 || value>(pow(10, fieldLen)-1))
  {
    sprintf(field, "%s", errorStr);
  }
  else
  {
    //Room to put a decimal or not
    if(value<=(pow(10, fieldLen-2)-1) && value>0)
    {
      dblFormat[1]=fieldLen+'0';
      sprintf(field, dblFormat, value);
    }
    else
    {
      decFormat[1]=fieldLen+'0';
      int intVal=value;
      sprintf(field, decFormat, intVal);
    }
  }

  //Write to the form
  geniePtr->WriteStr(objNum,field);
}
