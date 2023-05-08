#ifndef PropBag_h
#define PropBag_h

#include <EEPROM.h>

struct PropBagStruct
{
    //Calibration
    double_t pitotCalibration;
    double_t instMPGFactor;
};

class PropBag
{
  public:
    int getPropDataSize();
    void resetPropData();   
    void savePropData();    //Saves data to EEPROM
    void loadPropData();
    void dumpPropData();

    PropBagStruct data;

  private: 
};

#endif
