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
    void resetPropBag();   
    void savePropBag();    //Saves data to EEPROM
    void loadPropBag();
    void dumpPropBag();

    PropBagStruct data;

  private: 
};

#endif
