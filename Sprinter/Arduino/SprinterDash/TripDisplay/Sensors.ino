#include "Sensors.h"

void Barometer::init(int _refreshTicks)
{
    refreshTicks=_refreshTicks;
}

void Barometer::setup()
{
  if (!baro.begin()) 
  {
    Serial.println("Could not find MPL3115A2 Barometer");
    online=false;
  }
  else
  {
    online=true;
  }
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

    //Serial.print("Pressure = ");
    //Serial.println(pressure);
  }

  //Let's read elevation
  elevation = baro.getAltitude() * 3.28084;
  elevation = elevation + elevationOffset;

  //poor man's calibration
  if(elevation<0)
  {
    elevationOffset=elevationOffset-elevation;
    elevation = elevation + elevationOffset;
  }

  //Serial.print("Altitude = ");
  //Serial.println(elevation); 
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
    Serial.println("Couldn't find RTC");
    online=false;
    return;
  }

  if (! rtc.initialized() || rtc.lostPower()) 
  {
    Serial.println("RTC either lost power or is not initialized");
    online=false;
    return;
  }

  // When the RTC was stopped and stays connected to the battery, it has
  // to be restarted by clearing the STOP bit. Let's do this to ensure
  // the RTC is running.
  rtc.start();  
}

bool RTC::isOnline()
{
  return online;
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
  if(currentTime<=0 || currentTime<secondsSince2000 || (currentTime>secondsSince2000+360 && secondsSince2000>0))
  {
    //just ignore....
    online=false;
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
  _wire->begin();
  if(!isOnline())
  {
    _state = I2C_CONNECT_ERROR;
    return false;
  }
  _state = I2C_OK;
  return true;
}

uint8_t Pitot::getAddress()
{
  return PRESSURE_I2C_ADDRESS;
}

bool Pitot::isOnline()
{
  _wire->beginTransmission(PRESSURE_I2C_ADDRESS);
  return (_wire->endTransmission() == 0);
}

int Pitot::calibrate()
{

  _countOffset=0;

  //set zero point
  if(read())
  {
    _countOffset=MIN_COUNT-_sensorCount;
  }
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
    return retVal;

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

  //Smooth
  _mph = _tempmph*0.25 + _mph*0.75;

  return _mph;
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

  //adjust for zero and smooth
  _sensorCount=count + _countOffset;

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

    //Read state.  If off, double check
    ignState = digitalRead(IGN_PIN);
    if(!ignState)
    {
      delay(2000);
      ignState = digitalRead(IGN_PIN);
    }

    //Serial.print("IGN State: ");
    //Serial.println(ignState);

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

    //Serial.print("LDR:  ");  Serial.println(lightLevel);

    return lightLevel;
}
