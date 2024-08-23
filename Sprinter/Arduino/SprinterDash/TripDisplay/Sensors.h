#ifndef SENSORS_h
#define SENSORS_h

#include <Adafruit_MPL3115A2.h>
#include "RTClib.h"
#include "Filter.h"

/*
i2c addresses

barameter: 0x60
rtc: 0x68 
pressure: 0x28
*/

#define ATMOS_READ_INTERVAL 10   //How many altitude reads are done for each atmos pressure read

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
    bool online=false;
    int elevationOffset=0;
    int currentElevReadCount=0;

    //Filter for outlier values
    Filter filter;

    //Data
    int elevation=0;
    double pressure=0;

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
    uint32_t adjust();
    uint32_t adjust(unsigned long secondsSince2000);
    uint32_t adjust(DateTime dateTime);
    
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
  double   calibrate(int actualSpeed);
  void     setCalibrationFactor(double_t factor);
  int      readSpeed();
  int      state()        { return _state; };

private:
  int      read();
  int      calcSpeed();

  TwoWire*  _wire;
  uint32_t _sensorCount;
  int      _mph;
  double_t _calibrationFactor;
  
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