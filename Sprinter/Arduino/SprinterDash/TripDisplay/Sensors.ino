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
  }
}

bool Barometer::isOnline()
{
  return pressOnline&&AltOnline;
}

void Barometer::update()
{
  //don't update if it's not time to
  if(millis()<nextTickCount)
      return;
  nextTickCount=millis()+refreshTicks;

  //Is there a reading ready?
  readingReady=baro.conversionComplete();

  //Let's check if we need to start a new read or not
  if(!readingStarted && !readingReady)
  {
      //Serial.println("Starting non blocking barometer read");
      if(readingPressureNow)
          baro.setMode(MPL3115A2_BAROMETER);
      else
          baro.setMode(MPL3115A2_ALTIMETER);

      baro.startOneShot();
      return;
  }

  //If we're not quite ready, then come back
  if(readingStarted && !readingReady)
  {
    Serial.println("Baro not quite ready yet");
    return;
  }

  //Looks like our results are ready!
  if(readingPressureNow)
  {
    pressure = baro.getLastConversionResults(MPL3115A2_PRESSURE)/10.0;
    pressOnline=true;
    readingStarted=false;
    readingReady=false;
    readingPressureNow=false;

    //Serial.print("Pressure = ");
    //Serial.println(pressure);
  }
  else
  {
    elevation = baro.getLastConversionResults(MPL3115A2_ALTITUDE)* 3.28084;
    AltOnline=true;
    readingStarted=false;
    readingReady=false;
    readingPressureNow=true;

    //Account for negative altitude
    if(elevation<0)    
      elevation=1;  //1 means that we'll not try and reset the values

    //Serial.print("Altitude = ");
    //Serial.println(elevation); 
  }
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
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
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
  secondsSince2000=now.secondstime();

  //Mark sensor as online
  if(secondsSince2000>0)
    online=true;
  else
    online=false;

  return secondsSince2000;
}

//
// Calculating airspeed using a pitot and MPXV7002DP
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
//Put together:
//MPH = (sqrt(2*(((ADC/1241)*1000)/1.204)))*2.23694
//
//or simplified to:
//MPH = 2.83977*sqrt(ADC/Density)   - raw ADC reading and density of air in kg/m^3
//
//~2.5V == 0mph
//~3.3V == 82mph
//~3.7v == 100mph
//
//We could cut the voltage in half with a voltage divider.  Since we're baselining "zero", no adjustments to the calculations should be necessary.  (need to work out exactly how we're going to calibrate zero......button on display??)
//
//Regarding calculating air density, 3K ft only makes about 5MPH difference, so it may not be worth it.  However, we do have air pressure available so.....
//

//Pin used for analog reads
#define PITOT_ADC_PIN A0
#define AIR_DENSITY 1.204

void Pitot::init(int _refreshTicks)
{
  refreshTicks=_refreshTicks;

  //Are we getting reasonable readings?
  g_reference_pressure = analogRead(PITOT_ADC_PIN);  
  if(g_reference_pressure > 100 && g_reference_pressure < 4000)
    online=true;
}

void Pitot::calibrate()
{
  Serial.println("Calibrating pitot airspeed indicator");
  
  //Read sensor
  g_reference_pressure = analogRead(PITOT_ADC_PIN);  
  for (int i=1;i<=500;i++)
  {
    g_reference_pressure = (analogRead(PITOT_ADC_PIN))*0.25 + g_reference_pressure*0.75;
    delay(20);
  }
  
  g_air_pressure = g_reference_pressure;
}

bool Pitot::isOnline()
{
  return online;
}


int Pitot::readSpeed()
{
    //don't update if it's not time to
    if(millis()<nextTickCount)
        return g_airspeed_mph;

    //Update timing
    nextTickCount=millis()+refreshTicks;

    //Read pitot airspeed
    g_air_pressure = analogRead(PITOT_ADC_PIN)*0.25 + g_air_pressure*0.75;
    float pressure_diff = (g_air_pressure >= g_reference_pressure) ? (g_air_pressure - g_reference_pressure) : 0.0;  
    g_airspeed_mph = 2.83977*sqrt(pressure_diff/AIR_DENSITY);

    /*
    Serial.print("Pitot: (ref,pres,diff,mph) ");
    Serial.print(g_reference_pressure);
    Serial.print(" ");
    Serial.print(g_air_pressure);
    Serial.print(" ");
    Serial.print(pressure_diff);
    Serial.print(" ");
    Serial.println(g_airspeed_mph);
    */

    return g_airspeed_mph;
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

    //Read state
    ignState = digitalRead(IGN_PIN);

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

    //Read LDR
    for(int i=0;i<LDR_ADC_OVERSAMPLE;i++)
    {
      lightLevel = analogRead(LDR_ADC_PIN)*0.25 + lightLevel*0.75;
      delay(10);
    }

    //Serial.print("LDR:  ");  Serial.println(lightLevel);

    return lightLevel;
}
