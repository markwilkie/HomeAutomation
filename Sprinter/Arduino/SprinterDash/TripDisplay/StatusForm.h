#ifndef StatusForm_h
#define StatusForm_h

#include <genieArduino.h>

class StatusForm 
{
  public:
    StatusForm(Genie* geniePtr,int formID);

    int getFormId();
    void updateTitle(const char* title);
    void updateText(const char* text);

    bool needsUpdate=true;

  private: 

    //State
    int formID;

    //Display
    Genie *geniePtr;     
};

#endif
