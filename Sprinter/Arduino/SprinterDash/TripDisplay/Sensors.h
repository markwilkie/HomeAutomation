#ifndef SENSORS_h
#define SENSORS_h

#include <Adafruit_MPL3115A2.h>
#include "RTClib.h"

/*
i2c addresses

barameter: 0x60
rtc: 0x68 
pressure: 0x28
*/

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

#define PRESSURE_I2C_ADDRESS 0x28
#define MPH_RANGE 101
#define MIN_COUNT 1638 //(0%)
#define MAX_COUNT 14746 //(100%)
#define PASCAL_RANGE 1250
#define AIR_DENSITY 1.225
#define MS_2_MPH 2.23694

#define I2C_OK                        1
#define I2C_INIT                      0
#define I2C_READ_ERROR               -1
#define I2C_C000_ERROR               -2
#define I2C_CONNECT_ERROR            -3


class Pitot
{
public:

  bool     init(int);
  bool     isOnline();
  uint8_t  getAddress();


  //  returns status OK (0) or ERROR ( not 0 )
  int      calibrate();
  int      readSpeed();
  int      state()        { return _state; };

private:
  int      read();

  TwoWire*  _wire;
  uint32_t _sensorCount;
  int      _countOffset;
  int      _mph;
  
  uint8_t  _state;
  int refreshTicks;
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