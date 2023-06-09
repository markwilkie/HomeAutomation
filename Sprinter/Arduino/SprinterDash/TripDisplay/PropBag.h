#ifndef PropBag_h
#define PropBag_h

#include <EEPROM.h>

#define PITOT_CALIB 1.07
#define INST_MPG_FACTOR 1.78

struct PropBagStruct
{
    //Calibration
    double_t pitotCalibration=PITOT_CALIB;
    double_t instMPGFactor=INST_MPG_FACTOR;
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
