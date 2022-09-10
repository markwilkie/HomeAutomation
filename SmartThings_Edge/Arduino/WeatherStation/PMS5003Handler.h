#ifndef pms_h
#define pms_h

#include "PMS5003.h"
#include "logger.h"
#include "debug.h"

#define CONCEN_HIST_SIZE    12

struct breakPoint 
{
  const char* label;
  float conc_lo;
  float conc_hi;
  float AQI_lo;
  float AQI_hi;
};

struct concenReading
{
  unsigned long epoch;
  int pm25;
  int pm100;
};

class PMS5003Handler 
{

  public:
    void init();
    bool storeSamples();
    int getLastReadTime();
    int getPM10Standard();
    int getPM25Standard();
    int getPM100Standard();
    void calcBothAQI();
    int getPM25AQI();
    const char *getPM25Label();
    int getPM100AQI();
    const char *getPM100Label();

    int getPM0_3um();
    int getPM2_5um();
    int getPM10_0um();
    
  private: 
    void calcPMSMinMax(float &pm25min,float &pm25max,float &pm100min,float &pm100max,int &count);
    float calcPMSWeight(float min,float max);
    void sumWeights(int count,float weight,float &v1P25Sum,float &v2P25Sum,float &v1P100Sum,float &v2P100Sum);
    const char* calcAQI(float nowCast,struct breakPoint*,int &AQI);
    void calcPM25AQI();
    void calcPM100AQI();

    //Sensor itself
    PMS5003 pmsSensor; // I2C

    //concentration vars
    struct concenReading concenHistory[CONCEN_HIST_SIZE];
    int pm25AQI, pm100AQI;
    const char* pm25Label;
    const char* pm100Label;
    int currentIdx;

    //Breakpoint data for calculations
    struct breakPoint pm25BreakPoints[6] = {
                            {"Good", 0, 12, 0, 50},
                            {"Moderate", 12.1, 35.4, 51, 100},
                            {"Sensitive", 35.5, 55.4, 101, 150},
                            {"Unhealthy", 55.5, 150.4, 151, 200},
                            {"Very Unhealthy", 150.5, 250.4, 201, 300},
                            {"Hazardous", 250.5, 500.5, 301, 500}
                        };

    struct breakPoint pm100BreakPoints[6] = {
                            {"Good", 0, 54, 0, 50},
                            {"Moderate", 55, 154, 51, 100},
                            {"Sensitive", 155, 254, 101, 150},
                            {"Unhealthy", 255, 354, 151, 200},
                            {"Very Unhealthy", 355, 424, 201, 300},
                            {"Hazardous", 425, 604, 301, 500}
                        };

};

#endif
