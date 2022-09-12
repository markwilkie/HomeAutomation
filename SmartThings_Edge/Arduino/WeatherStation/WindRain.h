#include <Arduino.h>
#include "logger.h"
#include "debug.h"
#include "WeatherStation.h"

extern Logger logger;

#ifndef WINDRAIN_H
#define WINDRAIN_H

#define TIMEFACTOR        1000000UL   // factor between seconds and microseconds
#define WINDFACTOR        3.4         // 3.4 km/h per revolution, or 20 pulses
#define RAINFACTOR        0.0204      // bucket size

class WindRain 
{

  public:
    float getWindSpeed(long);
    float getWindGustSpeed();
    float getRainRate();
    
  private: 
};

#endif
