#ifndef windrainhandler_h
#define windrainhandler_h

#include "WindRain.h"
#include "logger.h"
#include "debug.h"

//  Setup
#define WIND_VANE_PIN      36         // wind vane pin */
#define LOWWIND             2         // Below this, we'll consider the gust limit to be kinda pointless
#define GUSTLIMIT           7         // x times wind speed.  supposed to catch glithes by making sure gust is not cazy higher than the wind speed generally
#define WIND_DIR_OFFSET   -70         // calibrate the wind vane

//externals
extern unsigned long currentTime();
extern double round2(double);

class WindRainHandler 
{

  public:
    void storeSamples();
    double getWindSpeed();
    double getWindGustSpeed();
    double getCurrentRainRate();
    double getLastHourRainRate();
    double getLast12RainRate();
    int getDirectionInDeg();
    String getDirectionLabel();
    
  private: 
    WindRain windRain;
    
    double windSpeed;
    double windGustSpeed;
    double currentRainRate;
    double lastHourRainRate;
    double last12RainRate;
    int rawDirectioninDegrees;    

    int calcWindDirection();

    //private vars
    double maxGust=0;
    unsigned long lastReadingTime;

    //used to calc avg speed
    double totalSpeed;
    int speedSamples;
};

#endif
