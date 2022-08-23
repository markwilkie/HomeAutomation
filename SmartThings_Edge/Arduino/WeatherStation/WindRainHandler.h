#ifndef windrainhandler_h
#define windrainhandler_h

#include "WindRain.h"
#include "logger.h"
#include "debug.h"

//
//  Setup
#define WIND_VANE_PIN       36                               // wind vane pin */

//Time keeping
extern int currentTime();

class WindRainHandler 
{

  public:
    void storeSamples();
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
    long lastReadingTime;

    //used to calc avg speed
    float totalSpeed;
    int speedSamples;
};

#endif
