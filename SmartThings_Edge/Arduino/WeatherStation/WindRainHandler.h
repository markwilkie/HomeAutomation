#ifndef windrainhandler_h
#define windrainhandler_h

#include "WindRain.h"
#include "debug.h"

//
//  Setup
#define WIND_VANE_PIN       36                               // wind vane pin */

class WindRainHandler 
{

  public:
    void storeSamples(int);
    float getWindSpeed();
    float getWindGustSpeed();
    float getRainRate();
    int getWindDirection();
    
  private: 
    WindRain windRain;
    
    float windSpeed;
    float windGustSpeed;
    float rainRate;
    int windDirection;    

    int calcWindDirection();

    //private vars
    float maxGust=0;
    float avgWindSpeed=0;
};

#endif
