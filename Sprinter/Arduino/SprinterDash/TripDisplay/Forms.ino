#include "Forms.h"
#include "TripData.h"
#include "genieArduino.h"

#define STATUS_TITLE_STRING 35
#define STATUS_STRING 34

#define HEATER_FUEL_STRING 32
#define MILES_DRIVEN_STRING 29
#define TIME_ELAPSED_STRING 30

#define MILES_SINCE_STOP_STRING 4
#define HOURS_ELAPSED_STRING 5
#define GALLONS_EXPECTED_STRING 9

//
// Starting Form
//
// first segment in a linked list  (each tripsegment has the next one)
StartingForm::StartingForm(Genie* _geniePtr,int _formID,TripData *_tripSegDataPtr,int _refreshTicks)
{
    //Save main genie pointer
    geniePtr=_geniePtr;

    //Save context
    formID=_formID;
    tripSegDataPtr=_tripSegDataPtr;
    refreshTicks=_refreshTicks;
}

int StartingForm::getFormId()
{
    return formID;
}

//Update display
void StartingForm::updateDisplay()
{
  //don't update if it's not time to
  if(millis()<nextTickCount)
      return;
  nextTickCount=millis()+refreshTicks;

  //Update time data fields
  strField.updateField(geniePtr,HEATER_FUEL_STRING, heaterFuel, tripSegDataPtr->getHeaterGallonsUsed());
  strField.updateField(geniePtr,MILES_DRIVEN_STRING, elapsedTime, tripSegDataPtr->getMilesTravelled());
  strField.updateField(geniePtr,TIME_ELAPSED_STRING, milesTravelled,tripSegDataPtr->getElapsedTime());
}


//
// Stopped Form
//
// first segment in a linked list  (each tripsegment has the next one)
StoppedForm::StoppedForm(Genie* _geniePtr,int _formID,TripData *_tripSegDataPtr,int _refreshTicks)
{
    //Save main genie pointer
    geniePtr=_geniePtr;
    formID=_formID;
    refreshTicks=_refreshTicks;

    tripSegDataPtr=_tripSegDataPtr;
}

int StoppedForm::getFormId()
{
    return formID;
}

//Update display
void StoppedForm::updateDisplay()
{
  //don't update if it's not time to
  if(millis()<nextTickCount)
      return;
  nextTickCount=millis()+refreshTicks;

  //Update time data fields
  strField.updateField(geniePtr,MILES_SINCE_STOP_STRING, milesTravelled, tripSegDataPtr->getMilesTravelled());
  strField.updateField(geniePtr,HOURS_ELAPSED_STRING, elapsedTime, tripSegDataPtr->getElapsedTime());
  strField.updateField(geniePtr,GALLONS_EXPECTED_STRING, gallonsExpected,tripSegDataPtr->getGallonsExpected());
}

//
// Status Form
//
StatusForm::StatusForm(Genie* _geniePtr,int _formID,int _refreshTicks)
{
    //Save main genie pointer
    geniePtr=_geniePtr;

    //Save context
    formID=_formID;
    refreshTicks=_refreshTicks;
}

int StatusForm::getFormId()
{
    return formID;
}

//Update display
void StatusForm::updateText(const char* text)
{
  //don't update if it's not time to
  if(millis()<nextTickCount)
      return;
  nextTickCount=millis()+refreshTicks;

  geniePtr->WriteStr(STATUS_STRING,text);
}

//Update display
void StatusForm::updateText(int value)
{
  //don't update if it's not time to
  if(millis()<nextTickCount)
      return;
  nextTickCount=millis()+refreshTicks;

  sprintf(fieldBuffer, "%d", value);
  geniePtr->WriteStr(STATUS_STRING,fieldBuffer);
}

void StatusForm::updateTitle(const char* title)
{
  //Update the 2 fields
  geniePtr->WriteStr(STATUS_TITLE_STRING, title);
}

