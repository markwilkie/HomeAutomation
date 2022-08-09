#ifndef windrainhandler_h
#define windrainhandler_h

#include "WindRain.h"
#include "debug.h"

//
//  Setup
#define WINDRAIN_SAMPLE_SEC TIMEDEEPSLEEP                    //# of seconds we're sampling (counting pulses) for at a time  
#define GUST_LAST12_SIZE    ((12*60*60)/WINDRAIN_SAMPLE_SEC) //# of max gusts (one per period) we need to store for the last 12 hours
#define RAIN_LAST12_SIZE    ((12*60*60)/WINDRAIN_SAMPLE_SEC)
#define WIND_VANE_PIN       36                               // wind vane pin */

typedef struct GustStructType
{
  float gust;
  float gustDirection;
  long gustTime;
};

class WindRainHandler 
{

  public:
    WindRainHandler();
    void storeSamples(int);
    float getWindSpeed();
    float getWindGustSpeed();
    float getRainRate();
    int getWindDirection();

    int getLast12MaxGustIdx();
    float getLast12MaxGustSpeed(int);
    int getLast12MaxGustDirection(int);
    long getLast12MaxGustTime(int);
    float getRainRateLastHour();
    float getMaxRainRateLast12();
    float getTotalRainLast12();    
    
  private: 
    struct GustStructType gustLast12[GUST_LAST12_SIZE];  //if space becomes an issue, we can move this to global and add static
    float rainLast12[RAIN_LAST12_SIZE];
    WindRain windRain;

    int windSampleIdx;              //index for where in the sample array we are
    int rainSampleIdx;
    
    float windSpeed;
    float windGustSpeed;
    float rainRate;
    int windDirection;    

    int calcWindDirection();
};

#endif
