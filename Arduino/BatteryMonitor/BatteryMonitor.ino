#include <Wire.h>  

//component specific includes
#include <RTClib.h>
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>

//Helpers
#include "PrecADCList.h"
#include "LcdScreens.h"

//setup
RTC_DS3231 rtc;
Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();
PrecADCList precADCList = PrecADCList();

//Init onboard ADC buffer
#define ADC_SAMPLE_SIZE 10
FastRunningMedian<long,ADC_SAMPLE_SIZE,0> adcBuffer;

//Battery level globals
#define BAT_PIN A0
#define AH 310
#define CHARGE_EFFICIENCY 94
long mAhRemaining;
long batterymV;
double stateOfCharge;
long socReset;

//Thermistor (thermometer)
#define THERMISTORPIN A1   // which analog pin to connect      
#define THERMISTORNOMINAL 10000      // resistance at 25 degrees C
#define TEMPERATURENOMINAL 25   // temp. for nominal resistance (almost always 25 C)
#define BCOEFFICIENT 4050 //4038 // The beta coefficient of the thermistor (usually 3000-4000)
#define SERIESRESISTOR 9920   // the value of the 'other' resistor

//General Globals
long bootTime;
long startTime;
long currentTime;
int hz;

void setup() 
{
  Serial.begin(57600);
  Serial.println("Initializing...");

  //setup lcd
  setupLCD();

  //setup MEGA ADC to 2.56 for reading battery voltage
  //analogReference(INTERNAL2V56);

  //setup real time clock (RTC)
  rtc.begin();
  rtc.adjust(0);  //set to epoch as a baseline

  //Precision ADC circuit buffers
  precADCList.begin();

  //Let's start with an amp hour guess
  batterymV=calcBatteryMilliVolts(mvRead());
  stateOfCharge = calcSoC(batterymV);
  mAhRemaining=calcMilliAmpHoursRemaining(stateOfCharge);
  
  //init vars
  bootTime = rtc.now().unixtime();
  startTime=bootTime;
  hz=0;

  //Display
  Serial.println("Monitor Started");
  startLCD();
  printSetupCommands();
}

void loop() 
{
  //status vars
  static int loophz=0;
  loophz++;

  //check serial input for setup, or lcd button presses
  handleButtonPresses();
  readChar();

  //read precision ADCs 
  precADCList.read(); 
  
  //Every second, we'll take a break and do some work
  currentTime=rtc.now().unixtime();
  if(currentTime != startTime)
  { 
    //reset hz counter
    hz=loophz;
    loophz=0; 
        
    //Set new start time     
    startTime=currentTime;
    
    //Adding to the time based buffers
    precADCList.add();  

    //If backlight has been on long enough, turn off
    if(currentTime>(backlightOnTime+BACKLIGHT_DURATION))
      backlightOff();

    //Update LCD
    printCurrentScreen();      

    //Once a minute, we add/subtract the mA to the current battery charge  (only do this once)
    if(!(currentTime % 60))
    {
      //Get voltage from battery
      batterymV = calcBatteryMilliVolts(mvRead());

      //Adjust Ah left on battery based on last minute mAh flow
      adjustAh();

      //If ampflow is below a threshold, reset SoC and Ah based on voltage
      long mAhLastHourAvg=precADCList.getLastHourAvg();
      if(mAhLastHourAvg > -1000 && mAhLastHourAvg < 1000)
      {
        stateOfCharge = calcSoC(batterymV);
        mAhRemaining=calcMilliAmpHoursRemaining(stateOfCharge);
      }

      //
      // temperature compensation for SoC
      //
      // - read thermister
      // - different SoC table
      //
    }       
  }
}

void printStatus()
{ 
  char buffer[20];
  
  //print ADC status
  precADCList.printStatus();    
  
  //print general status
  Serial.println("============================");
  Serial.print("Free Ram: "); Serial.println(freeRam());
  Serial.print("SoC: "); Serial.println(stateOfCharge);
  Serial.print("Voltage: "); Serial.println(batterymV*.001);
  Serial.print("Temperature: "); Serial.println(temperatureRead());
  Serial.print("Ah flow: "); Serial.println(precADCList.getCurrent()*.001,3);
  Serial.print("Usable Ah left: "); Serial.println(mAhRemaining*.001);
  Serial.print("Hours left to 100% or 50%: "); Serial.println(calcHoursRemaining(precADCList.getCurrent()));  
  Serial.print("Hz: "); Serial.println(hz);
  Serial.print("Current Second: "); Serial.println(currentTime);
  Serial.print("Since SoC reset: "); Serial.println(buildTimeLabel(currentTime-socReset,buffer));
  Serial.print("Uptime: "); Serial.println(buildTimeLabel(startTime,buffer)); 
}

//Pass in your own buffer to fill (at least 20 chars long)
char *buildTimeLabel(long seconds,char *buffer)
{
  double timeVal=0.0;

  if(seconds>(60*60*24))  //days
  {
    timeVal=startTime/60.0/60.0/24.0;
    if(timeVal>=100)
      strcpy(buffer,"D99+");
    else
    {
      buffer[0]='D';
      dtostrf(timeVal, 3, 1, buffer+1);   
    }
  }
  else if(startTime>(60*60)) //hours
  {
    timeVal=startTime/60.0/60.0;
    buffer[0]='H';
    dtostrf(timeVal, 3, 1, buffer+1);    
  }
  else if(startTime>60) //minutes
  {
    timeVal=startTime/60.0;
    buffer[0]='M';
    dtostrf(timeVal, 3, 1, buffer+1);     
  }
  else 
  {
    buffer[0]='S';
    ltoa(startTime, buffer+1, 10);   //seconds
  }

  return buffer;
}

//Update the number of Ah remaining on the battery based on the last minute of current flow
void adjustAh()
{
    //Let's increment/decrement AH of the battery based on amperage flow as an estimate
    long mAhFlow=precADCList.getLastMinuteAvg();
  
    //If charge, add effeciency
    if(mAhFlow > 0)
      mAhFlow=mAhFlow*(CHARGE_EFFICIENCY * .01);
  
    //Ok, update amp hours - making sure we divide the flow by 60, as we'll do it 60 times in one hour
    mAhRemaining=mAhRemaining+(mAhFlow/60);  
}

//Returns mV from onboard ADC using a calibrated aref value
long mvRead()
{
  //Read ADC
  adcBuffer.clear();
  for(int i=0;i<ADC_SAMPLE_SIZE;i++)
  {
    adcBuffer.addValue(analogRead(BAT_PIN));
  }

  //Aref = 2.522v (calibrated)
  //Aref = 5.000v (calibrated)
  return (adcBuffer.getMedian() * 5000) / 1024 ; // convert readings to mv  
}

//Return temperature
double temperatureRead()
{
  //Read ADC
  adcBuffer.clear();
  for(int i=0;i<ADC_SAMPLE_SIZE;i++)
  {
    adcBuffer.addValue(analogRead(THERMISTORPIN));
  }  

  // convert the value to resistance
  float average = 1023.0 / adcBuffer.getMedian() - 1.0;
  average = SERIESRESISTOR / average;

  //Use Steinhart to convert to temperature
  float steinhart;
  steinhart = average / THERMISTORNOMINAL;     // (R/Ro)
  steinhart = log(steinhart);                  // ln(R/Ro)
  steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
  steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
  steinhart = 1.0 / steinhart;                 // Invert
  steinhart -= 273.15;  
  steinhart = steinhart*(9.0/5.0)+32.0; //convert to F

  return steinhart;
}
 
//Calc battery voltage taking into account the resistor divider
long calcBatteryMilliVolts(long mv)
{
  //voltage divider ratio = 5.137, r2/(r1+r2) where r1=10.33K and r2=2.497K
  return mv * 5.137;
  //return 12960;
}

//Return % state of charge based on battery level
double calcSoC(int mv)
{
  socReset=rtc.now().unixtime();  //used to know how long ago the SoC was last set
  
  //@50deg F --> 12.4V = 50%, 12:54V = 60%, 12.68V = 70%,  12.82V = 80%, 12.96V = 90%, 13.1V = 100%
  //
  // 3.33mv for each degree F temperature change
  //
  int full=13140; //@50deg F
  int empty=12400;

  //Adjust for temperature
  int offset=(50-temperatureRead())*3.33;
  full=full+offset;
  empty=empty+offset;

  //Calc increments per 1%, then percentage
  double incr=(full-empty)/50.0;  //gives voltage per 1% in mV
  double sos=(((mv-empty)/incr) + 50);

  if(sos<0)
    sos=0;

  return sos;
}

//Calc milli amp hours left in battery based on SoC (%)
long calcMilliAmpHoursRemaining(long soc)
{
  //return's 50% of total capacity, as anything below that is not usable
  long ah=AH*(soc*.01);
  double mAhRemaining=((ah-(AH/2))*1000);
  if(mAhRemaining<0 || soc==0)
    mAhRemaining=0;
    
  return mAhRemaining;
}

//Calc's how long the battery will last to 50% based on flow in mA passed in
//Use this to calc time left based on any of the inputs, hour, day, etc....
double calcHoursRemaining(long mAflow)
{ 
  //We should now be able to calc hours left.  If mAflow positive, return 999 if there are no amps
  double hoursRemaining;
  if(mAflow>=0)
  {
    long full=(((long)AH/2)*1000);
    hoursRemaining=(((double)full-mAhRemaining)/mAflow);  //hours to full charge
  }
  else
  {
    hoursRemaining=(mAhRemaining/(mAflow*-1.0)); //hours to 50% charge
  }

  return hoursRemaining;
}

int freeRam() 
{
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}

