#ifndef PressureSensor_h
#define PressureSensor_h

#include "Wire.h"
#include "Arduino.h"

#define PRESSURE_I2C_ADDRESS 0x28
#define PASCAL_RANGE 1250
#define MIN_COUNT 1638 //(0%)
#define MAX_COUNT 14746 //(100%)
#define AIR_DENSITY 1.204
#define MS_2_MPH 2.23694

#define I2C_OK                        1
#define I2C_INIT                      0
#define I2C_READ_ERROR               -1
#define I2C_C000_ERROR               -2
#define I2C_CONNECT_ERROR            -3


class PressureSensor
{
public:

  bool     begin();
  void     reset();
  bool     isConnected();
  uint8_t  getAddress();


  //  returns status OK (0) or ERROR ( not 0 )
  int      calibrate();
  int      read();
  int      readSpeed();


  //  returns the pressure of last successful read in mbar
  float    getPascal()  { return _pressure; };
  float    getMPH()       { return _mph;};


  //  # errors since last good read
  uint16_t errorCount()   { return _errorCount; };
  //  timestamp of last good read
  uint32_t lastRead()     { return _lastRead; };
  //  get the last state
  int      state()        { return _state; };


private:
  TwoWire*  _wire;

  uint32_t _sensorCount;
  int      _countOffset;
  int      _pressure;

  int      _mph;
  
  uint8_t  _state;
  uint32_t _errorCount;
  uint32_t _lastRead;
};

#endif
