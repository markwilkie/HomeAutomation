#ifndef SENSORS_h
#define SENSORS_h

#include <Adafruit_MPL3115A2.h>
#include "RTClib.h"

class Barometer 
{
  public:
    void init(int _refreshTicks);
    bool isOnline();
    void setup();
    void update();
    int getElevation();
    double getPressure();
    
  private: 
    Adafruit_MPL3115A2 baro;
    int refreshTicks;
    bool pressOnline=false;
    bool AltOnline=false;
    bool readingPressureNow = true;
    bool readingStarted = false;
    bool readingReady = false;

    //Data
    int elevation;
    double pressure;

    //Timing
    unsigned long nextTickCount;      //when to update/refresh the gauge again
};

//  RTC_PCF8523
//
// https://github.com/adafruit/RTClib/blob/master/src/RTClib.h   

class RTC 
{
  public:
    void init(int _refreshTicks);
    bool isOnline();
    void setup();
    uint32_t getSecondsSinc2000();
    
  private: 
    RTC_PCF8523 rtc;
    int refreshTicks;

    //Data
    uint32_t secondsSince2000;
    bool online=false;

    //Timing
    unsigned long nextTickCount;      //when to update/refresh the gauge again
};

class Pitot 
{
  public:
    void init(int _refreshTicks);
    bool isOnline();    
    void calibrate();
    int readSpeed();
    
  private: 
    int refreshTicks;
    bool online=false;

    //Data
    float g_reference_pressure;
    float g_air_pressure;
    float g_airspeed_mph;

    //Timing
    unsigned long nextTickCount;      //when to update/refresh the gauge again
};

class IgnState 
{
  public:
    void init(int _refreshTicks);
    bool getIgnState();
    
  private: 
    int refreshTicks;

    //Data
    bool ignState;

    //Timing
    unsigned long nextTickCount;      //when to update/refresh the gauge again
};

class LDR 
{
  public:
    void init(int _refreshTicks);
    int readLightLevel();
    
  private: 
    int refreshTicks;

    //Data
    int lightLevel;

    //Timing
    unsigned long nextTickCount;      //when to update/refresh the gauge again
};

#endif