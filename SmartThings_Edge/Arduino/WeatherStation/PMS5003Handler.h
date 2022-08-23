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
    
  private: 
    PMS5003 pmsSensor; // I2C

};

#endif
