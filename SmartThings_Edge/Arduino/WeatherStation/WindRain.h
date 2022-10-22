#include <Arduino.h>
#include "logger.h"
#include "debug.h"
#include "WeatherStation.h"

extern Logger logger;
extern unsigned long currentTime();

#ifndef WINDRAIN_H
#define WINDRAIN_H

#define TIMEFACTOR        1000000UL   // factor between seconds and microseconds
#define WINDFACTOR        3.4         // 3.4 km/h per revolution, or 20 pulses
#define RAINFACTOR        0.0204      // bucket size

class WindRain 
{

  public:
    double getWindSpeed(long);
    double getWindGustSpeed();
    void getRainRate(double &current, double &lastHour, double &last12);
    
  private: 
    unsigned long newCycleTime=0;
    unsigned long newHourTime=0;
    int lastHourRainPulseCount[3600/CYCLETIME];
    int last12RainPulseCount[12];
    int currentIdx=0; 
    int last12Idx=0; 
};

#endif
