#include "StatusForm.h"

#define STATUS_TITLE_STRING 35
#define STATUS_STRING 34

// first segment in a linked list  (each tripsegment has the next one)
StatusForm::StatusForm(Genie* _geniePtr,int _formID)
{
    //Save main genie pointer
    geniePtr=_geniePtr;

    //Save context
    formID=_formID;
}

int StatusForm::getFormId()
{
    return formID;
}

//Update display
void StatusForm::updateText(const char* text)
{
  geniePtr->WriteStr(STATUS_STRING,text);
}

void StatusForm::updateTitle(const char* title)
{
  //Update the 2 fields
  geniePtr->WriteStr(STATUS_TITLE_STRING, title);
}
