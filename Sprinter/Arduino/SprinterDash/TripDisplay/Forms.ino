#include "Forms.h"
#include "TripData.h"
#include "genieArduino.h"

#define STATUS_TITLE_STRING 35
#define STATUS_STATUS_STRING 36
#define STATUS_STRING 34

#define HEATER_FUEL_STRING 32
#define MILES_DRIVEN_STRING 29
#define TIME_ELAPSED_STRING 30

#define MILES_SINCE_STOP_STRING 4
#define HOURS_ELAPSED_STRING 5
#define GALLONS_EXPECTED_STRING 9
#define AVG_MPG_STRING_1 38

#define BOOT_STRING 39

//
// Boot Form
//
// Shown when car first turned on
BootForm::BootForm(Genie* _geniePtr,int _formID)
{
    //Save main genie pointer
    geniePtr=_geniePtr;

    //Save context
    formID=_formID;
}

int BootForm::getFormId()
{
    return formID;
}

//Update display
void BootForm::updateDisplay(char *message,int activeForm)
{
  if(activeForm==formID)
    geniePtr->WriteStr(BOOT_STRING,message);
}

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
  strField.updateNumberField(geniePtr,HEATER_FUEL_STRING, heaterFuel, tripSegDataPtr->getHeaterGallonsUsed(), sizeof(heaterFuel)-1);
  strField.updateNumberField(geniePtr,MILES_DRIVEN_STRING, elapsedTime, tripSegDataPtr->getMilesTravelled(), sizeof(elapsedTime)-1);
  strField.updateNumberField(geniePtr,TIME_ELAPSED_STRING, milesTravelled,tripSegDataPtr->getElapsedTime(), sizeof(milesTravelled)-1);
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
  strField.updateNumberField(geniePtr,MILES_SINCE_STOP_STRING, milesTravelled, tripSegDataPtr->getMilesTravelled(),sizeof(milesTravelled)-1);
  strField.updateNumberField(geniePtr,HOURS_ELAPSED_STRING, elapsedTime, tripSegDataPtr->getElapsedTime(),sizeof(elapsedTime)-1);

  //Create gallons expected + oz of additive and update form
  int gallExp=tripSegDataPtr->getGallonsExpected();
  int ozToAdd=tripSegDataPtr->getGallonsExpected()*FUEL_ADDITIVE_RATIO;
  sprintf(gallonsExpected, "%d (%doz)", gallExp, ozToAdd);
  geniePtr->WriteStr(GALLONS_EXPECTED_STRING,gallonsExpected);
  //strField.updateField(geniePtr,GALLONS_EXPECTED_STRING, gallonsExpected,tripSegDataPtr->getGallonsExpected(),sizeof(gallonsExpected)-1);

  //If we have a valid avg mpg, show it
  double avgMpgFlt=tripSegDataPtr->getAvgMPG();
  if(avgMpgFlt>0 && avgMpgFlt<30)
    strField.updateNumberField(geniePtr,AVG_MPG_STRING_1, avgMPG,avgMpgFlt,sizeof(avgMPG)-1);
  else
    geniePtr->WriteStr(AVG_MPG_STRING_1,"---");
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
  geniePtr->WriteStr(STATUS_TITLE_STRING, title);
}

void StatusForm::updateStatus(unsigned long totalMsg,unsigned long CRC,double perc)
{
  sprintf(fieldBuffer, "Perc: %0.2lf (%ld/%ld)",perc,totalMsg,CRC);
  geniePtr->WriteStr(STATUS_STATUS_STRING, fieldBuffer);
}

void StatusForm::updateStatus(const char* text)
{
  geniePtr->WriteStr(STATUS_STATUS_STRING, text);
}
