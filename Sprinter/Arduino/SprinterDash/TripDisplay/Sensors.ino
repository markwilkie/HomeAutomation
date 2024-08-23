#include "Sensors.h"
#include "VanWifi.h"

void Barometer::init(int _refreshTicks)
{
    refreshTicks=_refreshTicks;
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
}

bool Barometer::isOnline()
{
  return online;
}

void Barometer::update()
{
  //don't update if it's not time to
  if(millis()<nextTickCount)
      return;
  nextTickCount=millis()+refreshTicks;

  //handle interval
  currentElevReadCount++;

  //Alternate between altimeter and pressure readings because it takes almost 400ms per reading
  if(currentElevReadCount>ATMOS_READ_INTERVAL || pressure==0)
  {
    pressure = baro.getPressure();  //in pascals
    currentElevReadCount=0;
    return;

    //logger.log(VERBOSE,"Pressure: %f",pressure);
  }

  //Let's read elevation with some bounds checking thrown
  int baroElevation = baro.getAltitude() * 3.28084;

  //debg info
  if(!online && pressure!=0)
  {
    logger.log(INFO,"Barometer online!  Altitude: %d Pressure: %f",baroElevation,pressure); 
    online=true; 
  }
  
  //poor man's calibration
  if(baroElevation<0)
  {
    elevationOffset=elevationOffset-baroElevation;
    baroElevation = baroElevation + elevationOffset;
  }

  bool isNotOutlierFlag = filter.writeIfNotOutlier(baroElevation);
  if(isNotOutlierFlag)
    elevation = baroElevation;
  else
    logger.log(VERBOSE,"Elevation Outlier: %d   Avg: %d",baroElevation,filter.readAvg());  
}

int Barometer::getElevation()
{
  return elevation;
}

double Barometer::getPressure()
{
  return pressure;
}

void RTC::init(int _refreshTicks)
{
    refreshTicks=_refreshTicks;
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
//For every 1V increase, there's an increase of 1 KPa per datasheet
//Or, 1241 per volt on a 12bit ADC at 3.3v
//....
//Pa = ((ADC/1241)*1000) where Pa is Pascals and ADC is the adc reading raw
//
//Density of Air at sealevel at 20c is 1.204 kg/m^3
//therefore...
//Velocity (m/s) = sqrt(2*(Pa/1.204))
//and...
//MPH = Velocity*2.23694
//
//Regarding calculating air density, 3K ft only makes about 5MPH difference, so it may not be worth it.  However, we do have air pressure available so.....
//

bool Pitot::init(int _refreshTicks)
{
  refreshTicks=_refreshTicks;

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
  //this crashes esp32 for whatever reason
  //_wire->beginTransmission(PRESSURE_I2C_ADDRESS);
  //return (_wire->endTransmission() == 0);

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
    return retVal;
  }

  //Now convert to mph
  int _tempmph = calcSpeed();  

  //Add calibration
  if(_calibrationFactor>0.01)
  {
    _tempmph *= _calibrationFactor;
  }

  //Smooth
  _mph = _tempmph*0.25 + _mph*0.75;

  return _mph;
}

int Pitot::calcSpeed()
{
    //  Min = 1638 (0%)
  //  Max = 14746 (100%)
  //
  //  Mult pascal range by 10 to increase resolution
  int _pressure = map(_sensorCount, MIN_COUNT, MAX_COUNT, 0, (PASCAL_RANGE*10));
  if(_pressure<0)
    _pressure=0;

  //div by 10 to get to the actual pa number
  _pressure=_pressure/10.0;

  //Now convert to mph
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
    _state = I2C_C000_ERROR;  //  no documentation, bits may not be set?
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

void IgnState::init(int _refreshTicks)
{
    refreshTicks=_refreshTicks;
}

bool IgnState::getIgnState()
{
    //don't update if it's not time to
    if(millis()<nextTickCount)
        return ignState;

    //Update timing
    nextTickCount=millis()+refreshTicks;

    //Read state - accounting for noise
    int ignOn=0;
    int ignOff=0;
    for(int i=0;i<10;i++)
    {
      delay(50);
      if(digitalRead(IGN_PIN))
        ignOn++;
      else
        ignOff++;
    }
    if(ignOn>=ignOff)
      ignState=true;
    else
      ignState=false;

    //logger.log(VERBOSE,"IGN State: %d",ignState);

    return ignState;
}

//
// There's an LDR in the LCD case which uses the 5v power that's there and a 10K pulldown for the analog output
//

//Pin used for analog reads
#define LDR_ADC_PIN A2
#define LDR_ADC_OVERSAMPLE 5

void LDR::init(int _refreshTicks)
{
  refreshTicks=_refreshTicks;
}

int LDR::readLightLevel()
{
    //don't update if it's not time to
    if(millis()<nextTickCount)
        return lightLevel;

    //Update timing
    nextTickCount=millis()+refreshTicks;

    //Read LDR with some smoothing
    for(int i=0;i<LDR_ADC_OVERSAMPLE;i++)
    {
      lightLevel = analogRead(LDR_ADC_PIN)*0.25 + lightLevel*0.75;
      delay(10);
    }

    //logger.log(VERBOSE,"LDR:  %d",lightLevel);

    return lightLevel;
}
