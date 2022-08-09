#ifndef WINDRAIN_H
#define WINDRAIN_H

#include <Arduino.h>
#include "ULP.h"

#define TIMEFACTOR        1000000     // factor between seconds and microseconds
#define WINDFACTOR        3.4         // 3.4 km/h per revolution, or 20 pulses
#define RAINFACTOR        0.0204      // bucket size

class WindRain 
{

  public:
    WindRain();
    float getWindSpeed(int);
    float getWindGustSpeed();
    float getRainRate();
    
  private: 
};

#endif
