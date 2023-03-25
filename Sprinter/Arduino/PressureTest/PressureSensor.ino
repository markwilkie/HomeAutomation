#include "PressureSensor.h"


bool PressureSensor::begin()
{
  reset();
  _wire = &Wire;
  _wire->begin();
  if (! isConnected())
  {
    _state = I2C_CONNECT_ERROR;
    return false;
  }
  _state = I2C_OK;
  return true;
}

void PressureSensor::reset()
{
  _errorCount = 0;
  _lastRead = 0;
  _pressure = 0;
}

uint8_t PressureSensor::getAddress()
{
  return PRESSURE_I2C_ADDRESS;
}

bool PressureSensor::isConnected()
{
  _wire->beginTransmission(PRESSURE_I2C_ADDRESS);
  return (_wire->endTransmission() == 0);
}

int PressureSensor::calibrate()
{
  //set zero point
  if(read())
  {
    _countOffset=MIN_COUNT-_sensorCount;
  }
}

int PressureSensor::readSpeed()
{
  int retVal=read();

  if(retVal)
  {
    _mph = sqrt(2*(_pressure/AIR_DENSITY))*MS_2_MPH;  
  }
  return retVal;
}

int PressureSensor::read()
{
  _wire->requestFrom(PRESSURE_I2C_ADDRESS, 2);
  if (_wire->available() != 2)
  {
    _errorCount++;
    _state = I2C_READ_ERROR;
    return _state;
  }
  int count = _wire->read() * 256;  //  hi byte
  count    += _wire->read();        //  lo byte
  if (count & 0xC000)
  {
    _errorCount++;
    _state = I2C_C000_ERROR;  //  no documentation, bits may not be set?
    return _state;
  }

  _sensorCount=count;

  //  Min = 1638 (0%)
  //  Max = 14746 (100%)
  _pressure = map(count+_countOffset, MIN_COUNT, MAX_COUNT, 0, PASCAL_RANGE);
  if(_pressure<0)
    _pressure=0;

  _lastRead = millis();

  _state = I2C_OK;
  return _state;
}


//  -- END OF FILE --
