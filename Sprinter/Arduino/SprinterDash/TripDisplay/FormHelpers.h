#ifndef FormHelper_h
#define FormHelper_h

#include <genieArduino.h>
#include "logger.h"

//Field masks for sprintfs  (used by StrField class)
char dblFormat[]={'%','x','.','1','l','f','\0'};
char decFormat[]={'%','x','l','d','\0'};

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

//Conversion types for fields
#define STND_FIELD_CONVERSION 0
#define TIME_FIELD_CONVERSION 1
#define ELEV_FIELD_CONVERSION 2

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
    void updateField(Genie *geniePtr,int objNum,char *title,double value,int fieldLen);
    void updateField(Genie *geniePtr,int objNum,char *title,double value,int fieldLen,int conversionType);
    void updateField(Genie *geniePtr,int objNum,char *title,long value,int fieldLen,int conversionType);
    void updateField(Genie *geniePtr,int objNum,char *title,long value,int fieldLen,int conversionType,bool isDouble);

  private: 
    bool isFieldTooLong(long value,int fieldLen,bool isDouble);
    bool convertNumber(long number,char *field,int fieldLen,bool isDouble);
    bool convertHours(long hours,char *field,int fieldLen,bool isDouble);  
    bool convertElevation(long elevation,char *convertedText,bool isDouble);  
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

void StrField::updateField(Genie *geniePtr,int objNum,char *field,double value,int fieldLen)
{
  updateField(geniePtr,objNum,field,(long)value*10.0,fieldLen,STND_FIELD_CONVERSION,true);
}

void StrField::updateField(Genie *geniePtr,int objNum,char *field,double value,int fieldLen,int conversionType)
{
  updateField(geniePtr,objNum,field,(long)value*10.0,fieldLen,conversionType,true);
}

void StrField::updateField(Genie *geniePtr,int objNum,char *field,long value,int fieldLen,int conversionType)
{
  updateField(geniePtr,objNum,field,value,fieldLen,conversionType,false);
}

void StrField::updateField(Genie *geniePtr,int objNum,char *field,long value,int fieldLen,int conversionType,bool isDouble)
{
  bool conversionOK=false;

  //convert based on type
  switch (conversionType)
  {
    case STND_FIELD_CONVERSION:
      logger.log(VERBOSE,"Std Field Conv: field: %d value:%ld",objNum,value);
      conversionOK=convertNumber(value,field,fieldLen,isDouble);
      break;
    
    case TIME_FIELD_CONVERSION:
      logger.log(VERBOSE,"Time Field Conv: field: %d value:%ld",objNum,value);
      convertHours(value,field,fieldLen,isDouble);
      conversionOK=true;
      break;  
    
    case ELEV_FIELD_CONVERSION:
      logger.log(VERBOSE,"Elev Field Conv: field: %d value:%ld",objNum,value);
      conversionOK=convertElevation(value,field,isDouble);
      break;  

    default:
      break;
  }

  //Write to the form
  if(conversionOK)
  {
    geniePtr->WriteStr(objNum,field);
  }
  else
  {
    sprintf(field, "%s", "ERR");
    logger.log(VERBOSE,"Error updating field with value: (Obj Num=%d v=%ld, len=%d  pow=%ld)",objNum,(int)value,fieldLen,(long)(pow(10, fieldLen)-1));
  }
}

bool StrField::isFieldTooLong(long value, int fieldLen, bool isDouble)
{
  if(isDouble)
    value=value/10;

  if(value<0 || value>(pow(10, fieldLen)-1))
  {
    return true;
  }

  return false;
}

bool StrField::convertNumber(long value, char *field, int fieldLen, bool isDouble)
{
  //Value in range?
  if(isFieldTooLong(value,fieldLen,isDouble))
    return false;

  //Room to put a decimal or not?
  if(isDouble && value<=(pow(10, fieldLen-2)-1) && value>0 && fieldLen>2)
  {
    double dblValue=value/10.0;
    dblFormat[1]=fieldLen+'0';
    sprintf(field, dblFormat, dblValue);
  }
  else
  {
    value=value/10;
    decFormat[1]=fieldLen+'0';
    sprintf(field, decFormat, value);
  }  

  return true;
}

//Converts a long (hours) to minutes, hours, etc
// .5 hours --> 30m
// 3.5 hours --> 3.5H
// 36 hours --> 1.5D
// 168 hours --> 1W
// 720 hours --> 1M
//
// Does NOT update the form
bool StrField::convertHours(long hours, char *convertedText,int fieldLen,bool isDouble)
{
  if(isDouble)
    hours=hours/10;

  //display minutes?
  if(hours<1)
  {
    int minutes=hours*60;
    if(isFieldTooLong(minutes*10,fieldLen,false))  // add another zero
      return false;    
    sprintf(convertedText,"%dm",minutes);
    return true;
  }

  //display hours?
  if(hours<24)
  {
    if(isFieldTooLong(hours*10,fieldLen,false))  // add another zero
      return false;        
    sprintf(convertedText,"%ldH",hours);  
    return true;
  }

  //display days?
  if(hours<168)
  {
    double days=(double)hours/24.0;
    if(isFieldTooLong(days*10.0,fieldLen,true))  // add another zero
      return false;        
    sprintf(convertedText,"%.1fD",days);
    return true;
  }  

  //display weeks?
  if(hours<720)
  {
    double weeks=(double)hours/168.0;
    if(isFieldTooLong(weeks*10.0,fieldLen,true))  // add another zero
      return false;        
    sprintf(convertedText,"%.1fW",weeks);
    return true;
  }

  //well, apparently we're up to months now....
  double months=(double)hours/720.0;
    if(isFieldTooLong(months*10.0,fieldLen,true))  // add another zero
    return false;      
  sprintf(convertedText,"%.1fM",months);
  return true;
}

bool StrField::convertElevation(long elevation, char *field, bool isDouble)
{
  if(isDouble)
    elevation=elevation/10;

  //If elevation < 10K, leave it alone
  if(elevation<10000)
  {
    sprintf(field,"%ld",elevation);
    return true;
  }

  //Since we're here, time to convert with a 'K'
  double totalElev=(double)elevation/1000.0;
  sprintf(field,"%.1fK",totalElev);
  return true;
}

#endif
