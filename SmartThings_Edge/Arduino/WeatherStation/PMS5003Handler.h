#ifndef pms_h
#define pms_h

#include "PMS5003.h"
#include "debug.h"

//sample setup
#define PMS_SAMPLE_SEC     600                           //how often we're reading the sensor in seconds
#define PMS_LAST12_SIZE   ((12*60*60)/PMS_SAMPLE_SEC)    //# of samples we need to store for last 12

class PMS5003Handler 
{

  public:
    void init();
    void storeSamples();
    int getPM10Standard();
    int getPM25Standard();
    int getPM100Standard();
    
  private: 
    PMS5003 pmsSensor; // I2C

    int pmsSampleIdx;              //index for where in the sample array we are
};

#endif
