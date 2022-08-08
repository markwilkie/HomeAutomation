#ifndef WINDRAIN_H
#define WINDRAIN_H

#include <Arduino.h>
#include "ULP.h"

#define WINDFACTOR        3.4         // 3.4 km/h per revolution, or 20 pulses
#define RAINFACTOR        0.0204      // bucket size

class WindRain 
{

  public:
    WindRain();
    float getWindSpeed(int);
    float getWindGustSpeed(int);
    float getRainRate();
    
  private: 
};

#endif
