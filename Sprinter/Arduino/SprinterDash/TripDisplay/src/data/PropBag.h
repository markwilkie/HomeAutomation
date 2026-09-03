#ifndef PropBag_h
#define PropBag_h

#include <EEPROM.h>

#define PITOT_CALIB 1.07
#define INST_MPG_FACTOR 1.78
#define GPS_EEPROM_OFFSET 400

struct PropBagStruct
{
    //Calibration
    double_t pitotCalibration=PITOT_CALIB;
    double_t instMPGFactor=INST_MPG_FACTOR;

    //Auto-calibration offset for barometric altimeter (feet).
    int elevationOffset=0;
};

class PropBag
{
  public:
    int getPropDataSize();
    void resetPropBag();   
    void savePropBag();    //Saves data to EEPROM
    void loadPropBag();
    void dumpPropBag();
    void saveGpsPosition();
    void loadGpsPosition();

    PropBagStruct data;

    //Last known GPS position — stored at fixed EEPROM offset to avoid shifting trip data
    float lastLat=0;
    float lastLon=0;

  private: 
};

#endif
