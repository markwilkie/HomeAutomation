#ifndef Forms_h
#define Forms_h

#include "FormHelpers.h"
#include <genieArduino.h>

class BootForm 
{
  public:
    BootForm(Genie* geniePtr,int formID);

    int getFormId();
    void updateDisplay(char *message,int activeForm);  

  private: 
    StrField strField;

    //State
    int formID;

    //Display
    Genie *geniePtr;      
};

class StartingForm 
{
  public:
    StartingForm(Genie* geniePtr,int formID,TripData *tripSegDataPtr,int _refreshTicks);

    int getFormId();
    void updateDisplay();

  private: 
    StrField strField;

    //State
    int formID;
    int refreshTicks;
    unsigned long nextTickCount;      //when to update/refresh    

    //Display
    Genie *geniePtr;     
    TripData *tripSegDataPtr;  

    //Fields
    char heaterFuel[3];
    char elapsedTime[5];
    char milesTravelled[5];
};

class StoppedForm 
{
  public:
    StoppedForm(Genie* geniePtr,int formID,TripData *tripSegDataPtr,int _refreshTicks);

    int getFormId();
    void updateDisplay();

  private: 
    StrField strField;

    //State
    int formID;
    const char* label;
    int refreshTicks;
    unsigned long nextTickCount;      //when to update/refresh    

    //Display
    Genie *geniePtr;     
    TripData *tripSegDataPtr;  

    //Fields
    char gallonsExpected[10];  //it's big because it also shows oz of additive
    char elapsedTime[5];
    char milesTravelled[5];
    char avgMPG[5];
};

class StatusForm 
{
  public:
    StatusForm(Genie* geniePtr,int formID,int _refreshTicks);

    int getFormId();
    void updateTitle(const char* title);
    void updateStatus(unsigned long totalMsg,unsigned long CRC,double perc);
    void updateStatus(const char* text);
    void updateText(const char* text);
    void updateText(int number);

    bool needsUpdate=true;

  private: 

    //State
    int formID;
    int refreshTicks;
    unsigned long nextTickCount;      //when to update/refresh    

    //Display
    Genie *geniePtr;
    char fieldBuffer[50];
};

#endif
