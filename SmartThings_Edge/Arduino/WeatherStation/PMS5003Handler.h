#ifndef pms_h
#define pms_h

#include "PMS5003.h"
#include "logger.h"
#include "debug.h"

#define CONCEN_HIST_SIZE    12

struct breakPoint 
{
  const char* label;
  double conc_lo;
  double conc_hi;
  double AQI_lo;
  double AQI_hi;
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
    void calcPMSMinMax(double &pm25min,double &pm25max,double &pm100min,double &pm100max,int &count);
    double calcPMSWeight(double min,double max);
    void sumWeights(int count,double weight,double &v1P25Sum,double &v2P25Sum,double &v1P100Sum,double &v2P100Sum);
    const char* calcAQI(double nowCast,struct breakPoint*,int &AQI);
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
                            {"Good", 0, 12.1, 0, 50},
                            {"Moderate", 12.1, 35.5, 51, 100},
                            {"Sensitive", 35.5, 55.5, 101, 150},
                            {"Unhealthy", 55.5, 150.5, 151, 200},
                            {"Very Unhealthy", 150.5, 250.5, 201, 300},
                            {"Hazardous", 250.5, 500.6, 301, 500}
                        };

    struct breakPoint pm100BreakPoints[6] = {
                            {"Good", 0, 55, 0, 50},
                            {"Moderate", 55, 155, 51, 100},
                            {"Sensitive", 155, 255, 101, 150},
                            {"Unhealthy", 255, 355, 151, 200},
                            {"Very Unhealthy", 355, 425, 201, 300},
                            {"Hazardous", 425, 605, 301, 500}
                        };

};

#endif
