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
  strField.updateHoursField(geniePtr,DRIVING_TIME_STRING, drivingTime, tripSegDataPtr->getDrivingTime(),sizeof(drivingTime)-1);
  strField.updateHoursField(geniePtr,ELASPED_TIME_STRING, elapsedTime, tripSegDataPtr->getElapsedTime(),sizeof(elapsedTime)-1);
  strField.updateHoursField(geniePtr,TIME_STOPPED_STRING, stoppedTime,tripSegDataPtr->getStoppedTime(),sizeof(stoppedTime)-1);
  strField.updateNumberField(geniePtr,NUM_STOPS_STRING, numberOfStops, tripSegDataPtr->getNumberOfStops(),2);
  strField.updateHoursField(geniePtr,TIME_PARKED_STRING, parkedTime, tripSegDataPtr->getParkedTime(),sizeof(parkedTime)-1);

  //Update moving data fields
  strField.updateNumberField(geniePtr,MILES_STRING, milesTravelled, tripSegDataPtr->getMilesTravelled(),sizeof(milesTravelled)-1);
  strField.updateNumberField(geniePtr,GALLONS_STRING, gallonsUsed, tripSegDataPtr->getFuelGallonsUsed(),sizeof(gallonsUsed)-1);
  strField.updateNumberField(geniePtr,AVG_MPG_STRING, avgMPG, tripSegDataPtr->getAvgMPG(),sizeof(avgMPG)-1);
  strField.updateNumberField(geniePtr,AVG_MOV_SPEED_STRING, avgMovingSpeed, tripSegDataPtr->getAvgMovingSpeed(),sizeof(avgMovingSpeed)-1);
  strField.updateElevationField(geniePtr,ELEVATION_GAIN_STRING, elevationGain, tripSegDataPtr->getTotalClimb(),sizeof(elevationGain)-1);
}
