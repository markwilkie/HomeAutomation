#include <Arduino.h>
#include "Sensors.h"
#include "../Globals.h"
#include "../net/VanWifi.h"

void Barometer::init(int _refreshTicks, int _offset)
{
    refreshTicks=_refreshTicks;
    nextTickCount=_offset;
}

void Barometer::setup()
{
  if (!baro.begin()) 
  {
    logger.log(ERROR,"Could not find MPL3115A2 Barometer");
    online=false;
  }

  //For filtering out outlier values  (9 value window, 50 threshold, and 3x outlier override)
  filter.init(9, 50, 3);

  //Kick off the first conversion so we have data ASAP
  //Read pressure first so boost calculation has a valid bara immediately
  baro.setMode(MPL3115A2_BAROMETER);
  baro.startOneShot();
  baroState = BARO_CONVERTING;
  readingPressure = true;
}

bool Barometer::isOnline()
{
  return online;
}

void Barometer::update()
{
  //State machine: IDLE waits for refresh interval, then starts a conversion.
  //CONVERTING polls conversionComplete() — no blocking.
  switch(baroState)
  {
    case BARO_IDLE:
    {
      //don't start a new conversion until it's time
      if(millis()<nextTickCount)
          return;

      //Decide what to read next
      currentElevReadCount++;
      if(currentElevReadCount>ATMOS_READ_INTERVAL || pressure==0)
      {
        readingPressure=true;
        baro.setMode(MPL3115A2_BAROMETER);
      }
      else
      {
        readingPressure=false;
        baro.setMode(MPL3115A2_ALTIMETER);
      }

      //Kick off one-shot conversion (non-blocking since OST is already clear)
      baro.startOneShot();
      conversionStartTime = millis();
      baroState=BARO_CONVERTING;
      break;
    }

    case BARO_CONVERTING:
    {
      //Wait at least 550ms for conversion to complete before polling
      //Avoids hammering I2C bus with conversionComplete() polls which
      //can interfere with pitot sensor reads on the same bus
      if(millis() < conversionStartTime + 550)
        return;

      if(!baro.conversionComplete())
        return;

      //Conversion done — schedule next read
      nextTickCount=millis()+refreshTicks;
      baroState=BARO_IDLE;

      if(readingPressure)
      {
        //Read pressure result (same units as getPressure() — hPa)
        pressure = baro.getLastConversionResults(MPL3115A2_PRESSURE);
        currentElevReadCount=0;
        return;
      }

      //Read altitude result
      int baroElevation = baro.getLastConversionResults(MPL3115A2_ALTITUDE) * 3.28084;
      rawElevation = baroElevation;  // store true raw value before any offset

      if(!online && pressure!=0)
      {
        logger.log(INFO,"Barometer online!  Altitude: %d Pressure: %f",baroElevation,pressure); 
        online=true; 
      }
      
      //If no API-based calibration yet, use poor man's calibration to prevent negative readings
      //Once ElevationAPI sets a real offset, this block is effectively bypassed because
      //the API offset will keep readings reasonable.
      if(baroElevation<0 && (baroElevation*-1)>elevationOffset)
      {
        elevationOffset=baroElevation*-1;
      }

      bool isNotOutlierFlag = filter.writeIfNotOutlier(baroElevation);
      if(isNotOutlierFlag)
        elevation = baroElevation+elevationOffset;
      else
        logger.log(VERBOSE,"Elevation Outlier: %d   Avg: %d",baroElevation,filter.readAvg());
      break;
    }
  }
}

int Barometer::getElevation()
{
  return elevation;
}

// Returns the raw barometer reading before offset is applied.
// Used by ElevationAPI to compute the calibration offset.
int Barometer::getRawElevation()
{
  return rawElevation;
}

// Set the elevation offset (feet) from external calibration (ElevationAPI).
// Replaces the poor man's calibration with a DEM-derived value.
void Barometer::setElevationOffset(int offset)
{
  elevationOffset = offset;
  logger.log(INFO, "Barometer: elevation offset set to %d ft", offset);
}

int Barometer::getElevationOffset()
{
  return elevationOffset;
}

double Barometer::getPressure()
{
  return pressure;
}

void RTC::init(int _refreshTicks, int _offset)
{
    refreshTicks=_refreshTicks;
    nextTickCount=_offset;
}

void RTC::setup()
{
  if (!rtc.begin()) 
  {
    logger.log(ERROR,"Couldn't find RTC");
    online=false;
    return;
  }

  if (!rtc.initialized() || rtc.lostPower()) 
  {
    logger.log(ERROR,"RTC either lost power or is not initialized.");
    adjust();
    online=true;
  }

  if(!rtc.isrunning())
  {
    logger.log(ERROR,"RTC does not seem to be running");
    rtc.start(); 
  } 
}

bool RTC::isOnline()
{
  return online;
}

uint32_t RTC::adjust()
{
  DateTime dateTime=DateTime();
  dateTime.secondstime();
  secondsSince2000=adjust(dateTime);
  return secondsSince2000;
}

uint32_t RTC::adjust(unsigned long secondsToSet)
{
  DateTime dateTime=DateTime(secondsToSet+SECONDS_FROM_1970_TO_2000);  //need to add because of the library
  if(dateTime.isValid())
  {
    secondsSince2000=adjust(dateTime);
  }
  else
  {
    logger.log(ERROR,"Trying to set the time to something invalid: %lu",secondsToSet);
    secondsSince2000=adjust();
  }
  
  return secondsSince2000;
}

uint32_t RTC::adjust(DateTime dateTime)
{
  rtc.stop();
  delay(2000);  //let things calm down I guess
  rtc.adjust(dateTime);
  secondsSince2000=dateTime.secondstime();
  logger.log(WARNING,"Setting time to: %lu seconds.  (%d/%d/%d %d:%d:%d)",dateTime.secondstime(),dateTime.month(),dateTime.day(),dateTime.year(),dateTime.hour(),dateTime.minute(),dateTime.second());
  
  //Ok, start things back up again
  rtc.start();
  online=true;

  return secondsSince2000;
}

uint32_t RTC::getSecondsSinc2000()
{
  //don't update if it's not time to
  if(millis()<nextTickCount)
      return secondsSince2000;
  nextTickCount=millis()+refreshTicks;

  //get current time from clock
  DateTime now = rtc.now();

  //do quick sanity check
  uint32_t currentTime=now.secondstime();
  if(currentTime<=0 || currentTime<secondsSince2000 || (currentTime>secondsSince2000+3600 && secondsSince2000>0))
  {   
    DateTime dateTime=DateTime(currentTime+SECONDS_FROM_1970_TO_2000);
    logger.log(ERROR,"CurrentTime is out of bounds: %lu, or %d/%d/%d %d:%d:%d. (Was %lu)  ",currentTime,dateTime.month(),dateTime.day(),dateTime.year(),dateTime.hour(),dateTime.minute(),dateTime.second(),secondsSince2000);    
    online=false;

    //Let's try and reset things
    adjust(secondsSince2000);
  }
  else
  {
    secondsSince2000=currentTime;
    online=true;
  }

  return secondsSince2000;
}

//
// Calculating airspeed using a pitot and the Sensata P1J-12.5MB-AX16PA sensor
//

bool Pitot::init(int _refreshTicks, int _offset)
{
  refreshTicks=_refreshTicks;
  nextTickCount=_offset;

  _wire = &Wire;
  if(!_wire->begin())
  {
    _state=I2C_CONNECT_ERROR;
    return false;
  }

  _state = I2C_OK;  //don't currently having a way to verify
  return true;  
}

uint8_t Pitot::getAddress()
{
  return PRESSURE_I2C_ADDRESS;
}

bool Pitot::isOnline()
{
  if(!_state==I2C_OK)
    return false;

  return true;
}

//Create factor of pitot speed vs. actual speed
double_t Pitot::calibrate(int actualSpeed)
{
  //Get calc airspeed
  int airSpeed = calcSpeed();  

  //Determine factor
  _calibrationFactor=0;
  if(actualSpeed>0 && airSpeed>0)
    _calibrationFactor=(double_t)actualSpeed/(double_t)airSpeed;
  
  return _calibrationFactor;
}

//Used to load calibration from EEPROM
void Pitot::setCalibrationFactor(double_t _factor)
{
  _calibrationFactor=_factor;
}

int Pitot::readSpeed()
{
  //don't update if it's not time to
  if(millis()<nextTickCount)
      return _mph;
  nextTickCount=millis()+refreshTicks;

  //read sensor
  int retVal=read();
  if(retVal<0)
  {
    logger.log(ERROR,"Error reading pressure sensor (pitot)");
    return _mph;  //return last good value, not error code
  }

  //Now convert to mph
  int _tempmph = calcSpeed();  

  //Add calibration
  if(_calibrationFactor>0.01)
  {
    _tempmph *= _calibrationFactor;
  }

  //Slew rate limiter: cap change to 5 mph/sec to reject fast manifold-vacuum spikes
  //while allowing real airspeed changes to track normally
  unsigned long now = millis();
  float elapsed = (now - _lastReadTime) / 1000.0;
  _lastReadTime = now;
  if(elapsed > 0 && elapsed < 2.0)  //skip first call and guard against stale timestamps
  {
    int maxChange = 5 * elapsed;  //5 mph per second max
    if(maxChange < 1) maxChange = 1;
    int delta = _tempmph - _mph;
    if(delta > maxChange) delta = maxChange;
    if(delta < -maxChange) delta = -maxChange;
    _tempmph = _mph + delta;
  }

  //Light EMA on top of slew-limited value
  _mph = _tempmph*0.25 + _mph*0.75;

  //Diagnostic: log raw sensor data to help diagnose throttle correlation
  logger.log(VERBOSE,"Pitot raw=%lu calc=%d calib=%d slew=%d smooth=%d", _sensorCount, calcSpeed(), _tempmph, _tempmph, _mph);

  return _mph;
}

int Pitot::calcSpeed()
{
  int _pressure = map(_sensorCount, MIN_COUNT, MAX_COUNT, 0, (PASCAL_RANGE*10));
  if(_pressure<0)
    _pressure=0;

  _pressure=_pressure/10.0;

  int _tempmph = sqrt((2.0*_pressure)/AIR_DENSITY)*MS_2_MPH;  

  return _tempmph;
}

int Pitot::read()
{
  _wire->requestFrom(PRESSURE_I2C_ADDRESS, 2);
  if (_wire->available() != 2)
  {
    _state = I2C_READ_ERROR;
    return _state;
  }
  int count = _wire->read() * 256;  //  hi byte
  count    += _wire->read();        //  lo byte
  if (count & 0xC000)
  {
    _state = I2C_C000_ERROR;
    return _state;
  }

  _sensorCount=count;

  _state = I2C_OK;
  return _state;
}

//
// Reads pin from OBDII which will be high if ignition is on
//

//Pin used for reads
#define IGN_PIN D4

void IgnState::init(int _refreshTicks, int _offset)
{
    refreshTicks=_refreshTicks;
    nextTickCount=_offset;
}

bool IgnState::getIgnState()
{
    //Sample one digitalRead per call, spaced 50ms apart (non-blocking)
    if(millis()<nextTickCount)
        return ignState;
    nextTickCount=millis()+50;

    //Accumulate samples
    if(digitalRead(IGN_PIN))
      ignOnCount++;
    else
      ignOffCount++;
    sampleCount++;

    //After 10 samples (~500ms total), decide
    if(sampleCount>=10)
    {
      ignState=(ignOnCount>=ignOffCount);
      ignOnCount=0;
      ignOffCount=0;
      sampleCount=0;
    }

    return ignState;
}

//
// There's an LDR in the LCD case which uses the 5v power that's there and a 10K pulldown for the analog output
//

//Pin used for analog reads
#define LDR_ADC_PIN A2
#define LDR_ADC_OVERSAMPLE 5

void LDR::init(int _refreshTicks, int _offset)
{
  refreshTicks=_refreshTicks;
  nextTickCount=_offset;
}

int LDR::readLightLevel()
{
    //Sample one ADC read per call, spaced refreshTicks/OVERSAMPLE_COUNT apart (non-blocking)
    if(millis()<nextTickCount)
        return lightLevel;
    nextTickCount=millis()+(refreshTicks/OVERSAMPLE_COUNT);

    //Accumulate samples
    sampleAccum += analogRead(LDR_ADC_PIN);
    sampleCount++;

    //After OVERSAMPLE_COUNT samples, average and smooth
    if(sampleCount>=OVERSAMPLE_COUNT)
    {
      int avg = sampleAccum / OVERSAMPLE_COUNT;
      lightLevel = avg * 0.25 + lightLevel * 0.75;
      sampleCount=0;
      sampleAccum=0;
    }

    return lightLevel;
}
