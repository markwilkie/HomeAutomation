#ifndef pms_h
#define pms_h

#include "PMS5003.h"
#include "logger.h"
#include "debug.h"

class PMS5003Handler 
{

  public:
    void init();
    bool storeSamples();
    int getPM10Standard();
    int getPM25Standard();
    int getPM100Standard();

    int getPM0_3um();
    int getPM2_5um();
    int getPM10_0um();
    
  private: 
    PMS5003 pmsSensor; // I2C

};

#endif
