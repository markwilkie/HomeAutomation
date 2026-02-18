#ifndef FormHelper_h
#define FormHelper_h

#include <genieArduino.h>
#include "../logging/logger.h"

extern Logger logger;

//Field masks for sprintfs  (used by StrField class)
extern char dblFormat[];
extern char decFormat[];

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

#endif
