#include <Arduino.h>
#include "logger.h"
#include "debug.h"
#include "WeatherStation.h"

extern Logger logger;
extern unsigned long currentTime();
extern String getTimeString();

#ifndef WINDRAIN_H
#define WINDRAIN_H

#define TIMEFACTOR        1000000UL   // factor between seconds and microseconds
#define WINDFACTOR        3.914639    // 1.75 m/s per revolution, 3.9mph, or 20 pulses
#define GUSTFACTOR        .225        // We're measuring a single pulse, so this is the "fudge factor" to get to revolutions per second
#define RAINFACTOR        0.0102      // 0.0204      // bucket size

class WindRain 
{

  public:
    double getWindSpeed(long);
    double getWindGustSpeed();
    void getRainRate(double &current, double &lastHour, double &last12);
    
  private: 
    unsigned long newCycleTime;
    unsigned long newHourTime;
    int lastHourRainPulseCount[3600/CYCLETIME];
    int last12RainPulseCount[12];
    int currentIdx; 
    int last12Idx; 
};

#endif
