#ifndef FormHelper_h
#define FormHelper_h

#include <genieArduino.h>
#include "logger.h"

//Field masks for sprintfs  (used by StrField class)
char dblFormat[]={'%','x','.','1','l','f','\0'};
char decFormat[]={'%','x','d','\0'};

//Form object numbers
#define PRIMARY_FORM 0
#define STOPPED_FORM 1
#define SUMMARY_FORM 2
#define STARTING_FORM 3
#define STATUS_FORM 4
#define BOOT_FORM 5
#define MENU_FORM 6
//#define BOOT_FORM 7

#define STOPPED_TO_PRIMARY_BUTTON 0
#define PRIMARY_TO_SUMMARY_BUTTON 1
#define MENU_TO_SUMMARY_BUTTON 2
#define SUMMARY_TO_PRIMARY_BUTTON 3
#define START_TO_PRIMARY_BUTTON 4
#define STOPPED_TO_MENU_BUTTON 5
#define STATUS_TO_PRIMARY_BUTTON 6
#define STATUS_TO_MENU_BUTTON 7
#define MENU_TO_PRIMARY_BUTTON 9
#define SUMMARY_TO_MENU_BUTTON 10
#define START_TO_MENU_BUTTON 11

#define CYCLE_SUMMARY_FORM_BUTTON 8

#define START_NEW_TRIP 0
#define START_NEW_SEGMENT 1

#define MENU_NEW_TRIP 4
#define MENU_NEW_SEGMENT 5
#define MENU_DEBUG 2
#define MENU_PITOT_CALIBRATION 3

//Actions
#define ACTION_NOP 0
#define ACTION_ACTIVATE_PRIMARY_FORM 1
#define ACTION_ACTIVATE_SUMMARY_FORM 2
#define ACTION_ACTIVATE_STARTING_FORM 3
#define ACTION_ACTIVATE_STATUS_FORM 5
#define ACTION_ACTIVATE_MENU_FORM 6

#define ACTION_START_NEW_TRIP 7
#define ACTION_START_NEW_SEGMENT 8
#define ACTION_PITOT 9
#define ACTION_DEBUG 11

#define ACTION_CYCLE_SUMMARY 10

class FormNavigator
{
  public:
    void init(Genie *geniePtr);
    int determineAction(genieFrame *Event);
    void activateForm(int formObjNumber);

    int getActiveForm();
    int getLastActiveForm();

  private:
    Genie *geniePtr;

    int currentActiveForm=BOOT_FORM;
    int lastActiveForm=BOOT_FORM;
};

class StrField 
{
  public:
    void updateHoursField(Genie *geniePtr,int objNum,char *title,double value,int fieldLen);
    void updateElevationField(Genie *geniePtr,int objNum,char *title,long value,int fieldLen);
    void updateNumberField(Genie *geniePtr,int objNum,char *title,double value,int fieldLen);

  private: 
    bool isFieldTooLong(long value,int fieldLen);
};


/////////////////////////////////////////////
///////////////////////////////////////////

void FormNavigator::init(Genie *_geniePtr)
{
  geniePtr=_geniePtr;
}

void FormNavigator::activateForm(int formObjNumber)
{
    logger.log(VERBOSE,"Activating form: %d",formObjNumber);
    lastActiveForm=currentActiveForm;
    currentActiveForm=formObjNumber;
    geniePtr->WriteObject(GENIE_OBJ_FORM,formObjNumber,0);
}

int FormNavigator::getActiveForm()
{
  return currentActiveForm;
}

int FormNavigator::getLastActiveForm()
{
  return lastActiveForm;
}

int FormNavigator::determineAction(genieFrame *event)
{
  int action=ACTION_NOP;

  //If the cmd received is from a Reported Event (Events triggered from the Events tab of Workshop4 objects)
  if (event->reportObject.cmd == GENIE_REPORT_EVENT)
  {
    //If user button was pressed
    if (event->reportObject.object == GENIE_OBJ_USERBUTTON)
    {
      switch(event->reportObject.index)
      {
        case SUMMARY_TO_PRIMARY_BUTTON:
        case STOPPED_TO_PRIMARY_BUTTON:
        case START_TO_PRIMARY_BUTTON:
        case STATUS_TO_PRIMARY_BUTTON:
        case MENU_TO_PRIMARY_BUTTON:
          action=ACTION_ACTIVATE_PRIMARY_FORM;
          break;
        case PRIMARY_TO_SUMMARY_BUTTON:
        case MENU_TO_SUMMARY_BUTTON:
          action=ACTION_ACTIVATE_SUMMARY_FORM;
          break;
        case CYCLE_SUMMARY_FORM_BUTTON:
          action=ACTION_CYCLE_SUMMARY;
          break;
        case START_TO_MENU_BUTTON:
        case STATUS_TO_MENU_BUTTON:
        case STOPPED_TO_MENU_BUTTON:
        case SUMMARY_TO_MENU_BUTTON:
          action=ACTION_ACTIVATE_MENU_FORM;
          break;
      }            
    }       

    // If a Winbutton was pressed
    if (event->reportObject.object == GENIE_OBJ_WINBUTTON) 
    {
      switch(event->reportObject.index)
      {
        case START_NEW_TRIP:
        case MENU_NEW_TRIP:
          action=ACTION_START_NEW_TRIP;
          break;
        case START_NEW_SEGMENT:
        case MENU_NEW_SEGMENT:
          action=ACTION_START_NEW_SEGMENT;
          break;
        case MENU_DEBUG:
          action=ACTION_DEBUG;
          break;
        case MENU_PITOT_CALIBRATION:
          action=ACTION_PITOT;
          break;                
      }    
    }
  }

  return action;
}  

bool StrField::isFieldTooLong(long value, int fieldLen)
{
  if(value<0 || value>(pow(10, fieldLen)-1))
  {
    return true;
  }

  return false;
}

void StrField::updateNumberField(Genie *geniePtr,int objNum,char *field,double number,int fieldLen)
{
  //Value in range?
  if(isFieldTooLong(number,fieldLen))
  {
    sprintf(field, "%s", "ERR");
    logger.log(VERBOSE,"Error updating field with value: (Obj Num=%d v=%f, len=%d  pow=%ld)",objNum,number,fieldLen,(long)(pow(10, fieldLen)-1));
    geniePtr->WriteStr(objNum,field);
    return;
  }

  //Room to put a decimal or not?
  if(number<=(pow(10, fieldLen-2)-1) && number>0 && fieldLen>2)
  {
    dblFormat[1]=fieldLen+'0';
    sprintf(field, dblFormat, number);
  }
  else
  {
    decFormat[1]=fieldLen+'0';
    int intVal=number;
    sprintf(field, decFormat, intVal);
  }  

  geniePtr->WriteStr(objNum,field);
}

//Converts a long (hours) to minutes, hours, etc
// .5 hours --> 30m
// 3.5 hours --> 3.5H
// 36 hours --> 1.5D
// 168 hours --> 1W
// 720 hours --> 1M
//
// Does NOT update the form
void StrField::updateHoursField(Genie *geniePtr,int objNum,char *field,double hours,int fieldLen)
{
  //display minutes?  (remember, if double, hours has an extra zero)
  if(hours<1.0)
  {
    int minutes=hours*60.0;
    if(isFieldTooLong(minutes*10,fieldLen))  // add another zero
      sprintf(field, "%s", "ERR");
    else    
      sprintf(field,"%dm",minutes);
    geniePtr->WriteStr(objNum,field);
    return;
  }

  //display hours?
  if(hours<24)
  {
    if(isFieldTooLong(hours*10,fieldLen))  // add another zero
      sprintf(field, "%s", "ERR");
    else      
      sprintf(field,"%.1fH",hours);  
    geniePtr->WriteStr(objNum,field);
    return;    
  }

  //display days?
  if(hours<168)
  {
    double days=(double)hours/24.0;
    if(isFieldTooLong(days*10.0,fieldLen))  // add another zero
      sprintf(field, "%s", "ERR");
    else         
      sprintf(field,"%.1fD",days);
    geniePtr->WriteStr(objNum,field);
    return;    
  }  

  //display weeks?
  if(hours<720)
  {
    double weeks=(double)hours/168.0;
    if(isFieldTooLong(weeks*10.0,fieldLen))  // add another zero
      sprintf(field, "%s", "ERR");
    else       
      sprintf(field,"%.1fW",weeks);
    geniePtr->WriteStr(objNum,field);
    return;
  }

  //well, apparently we're up to months now....
  double months=(double)hours/720.0;
  if(isFieldTooLong(months*10.0,fieldLen))  // add another zero
    sprintf(field, "%s", "ERR");
  else      
    sprintf(field,"%.1fM",months);
  geniePtr->WriteStr(objNum,field);
  return;
}

void StrField::updateElevationField(Genie *geniePtr,int objNum,char *field,long elevation,int fieldLen)
{
  //If elevation < 10K, leave it alone
  if(elevation<10000)
  {
    sprintf(field,"%ld",elevation);
  }
  else
  {
    //Since we're here, time to convert with a 'K'
    double totalElev=(double)elevation/1000.0;
    sprintf(field,"%.1fK",totalElev);
  }

  geniePtr->WriteStr(objNum,field);
}

#endif
