#ifndef BAROMETER_h
#define BAROMETER_h

#include <Adafruit_MPL3115A2.h>

class Barometer 
{
  public:
    void init(int _refreshTicks);
    void setup();
    void update();
    int getElevation();
    double getPressure();
    
  private: 
    Adafruit_MPL3115A2 baro;
    int refreshTicks;
    bool readingPressureNow = true;
    bool readingStarted = false;
    bool readingReady = false;

    //Data
    int elevation;
    double pressure;

    //Timing
    unsigned long nextTickCount;      //when to update/refresh the gauge again
};

#endif