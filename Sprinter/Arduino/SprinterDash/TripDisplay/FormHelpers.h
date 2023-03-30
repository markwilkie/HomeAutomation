#ifndef FormHelper_h
#define FormHelper_h

#include <genieArduino.h>

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
#define MENU_STATUS 2
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

    int currentActiveForm=0;
    int lastActiveForm=0;
};

class StrField 
{
  public:
    void updateField(Genie *geniePtr,int objNum,char *title,double value);
    void updateField(Genie *geniePtr,int objNum,char *title,double value,int fieldLen);

  private: 
};

/////////////////////////////////////////////
///////////////////////////////////////////

void FormNavigator::init(Genie *_geniePtr)
{
  geniePtr=_geniePtr;
}

void FormNavigator::activateForm(int formObjNumber)
{
    Serial.print("Activating form: "); Serial.println(formObjNumber);
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
        case MENU_STATUS:
          action=ACTION_ACTIVATE_STATUS_FORM;
          break;
        case MENU_PITOT_CALIBRATION:
          action=ACTION_PITOT;
          break;                
      }    
    }
  }

  return action;
}  


void StrField::updateField(Genie *geniePtr,int objNum,char *field,double value)
{
  updateField(geniePtr,objNum,field,value,sizeof(field));
}

void StrField::updateField(Genie *geniePtr,int objNum,char *field,double value,int fieldLen)
{
  //Value in range?
  if(value<0 || value>(pow(10, fieldLen)-1))
  {
    sprintf(field, "%s", "ERR");
  }
  else
  {
    //Room to put a decimal or not
    if(value<=(pow(10, fieldLen-2)-1) && value>0 && fieldLen>2)
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

#endif
