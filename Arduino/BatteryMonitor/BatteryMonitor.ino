#include <Wire.h>  

//component specific includes
#include <RTClib.h>
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>

//Helpers
#include "PrecADC.h"

//setup
RTC_DS3231 rtc;
Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();

//Get precision ADC's going
PrecADC lowPowerPanel = PrecADC(0,GAIN_TWOTHIRDS, 2482, .066);
PrecADC solarPanel = PrecADC(1,GAIN_TWOTHIRDS, 0, 0);
PrecADC inverter = PrecADC(2,GAIN_TWOTHIRDS, 0, 0);
PrecADC starterBattery = PrecADC(3,GAIN_TWOTHIRDS, 0, 0);

//Init onboard ADC buffer
#define ADC_SAMPLE_SIZE 10
FastRunningMedian<long,ADC_SAMPLE_SIZE,0> adcBuffer;

// These #defines make it easy to set the backlight color on LCD
#define OFF 0x0
#define RED 0x1
#define YELLOW 0x3
#define GREEN 0x2
#define TEAL 0x6
#define BLUE 0x4
#define VIOLET 0x5
#define WHITE 0x7

//Battery level globals
#define BAT_PIN A0
#define AH 310
#define CHARGE_EFFICIENCY 94
long mAhFlow;
long mAhRemaining;
long batterymV;
float stateOfCharge;
long socReset;

//General Globals
long startTime;
int hz;

void setup() 
{
  Serial.begin(57600);

  //setup lcd
  Serial.println("Starting...");
  lcd.begin(16,2);  //16 x 2 lcd
  lcdPrint("Starting....");
  lcd.setBacklight(OFF);

  //setup MEGA ADC to 2.56 for reading battery voltage
  analogReference(INTERNAL2V56);

  //setup real time clock (RTC)
  rtc.begin();
  rtc.adjust(0);  //set to epoch as a baseline

  //ADC
  lowPowerPanel.begin();
  solarPanel.begin();
  inverter.begin();
  starterBattery.begin();

  //Let's start with an amp hour guess
  batterymV=calcBatteryMilliVolts(readADC(BAT_PIN));
  stateOfCharge = calcSoC(batterymV);
  mAhRemaining=calcMilliAmpHoursRemaining(stateOfCharge);
  
  //init vars
  startTime = rtc.now().unixtime();
  hz=0;
}

void loop() 
{
  //status vars
  hz++;

  //read adc 
  lowPowerPanel.read(); //samples pin 
  solarPanel.read();
  inverter.read();
  starterBattery.read();  

  //Every second, we'll take a break and do some work
  long currentTime=rtc.now().unixtime();
  if(currentTime != startTime)
  { 
    //Set new start time     
    startTime=currentTime;
    
    //Adding to the time based buffers
    lowPowerPanel.add(); 
    solarPanel.add();
    inverter.add();
    starterBattery.add();  

    //Once a minute, we add/subtract the mA to the current battery charge  (only do this once)
    if(!(currentTime % 60))
    {
      //Get voltage from battery
      batterymV = calcBatteryMilliVolts(readADC(BAT_PIN));

      //Adjust Ah left on battery based on current flow
      adjustAh();

      //If ampflow is below a threshold, reset SoC and Ah based on voltage
      if(mAhFlow > -250 && mAhFlow < 250)
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
      
      //Dump
      printStatus();
    }    
    
    //reset hz counter
    hz=0;    
  }
}

void printStatus()
{
    //print status
    Serial.println("============================");
    Serial.print("Free Ram: "); Serial.println(freeRam());
    Serial.print("SoC: "); Serial.println(stateOfCharge);
    Serial.print("Voltage: "); Serial.println(batterymV*.001);
    Serial.print("Ah flow: "); Serial.println(mAhFlow*.001,3);
    Serial.print("Usable Ah left: "); Serial.println(mAhRemaining*.001);
    Serial.print("Hours left: "); Serial.println(calcHoursRemaining(mAhFlow));  //mAhFlow
    Serial.print("Hz: "); Serial.println(hz);
    Serial.print("Second: "); Serial.println(startTime);
    Serial.print("Since SoC reset: "); Serial.println(startTime-socReset);
    
    if(startTime>(60*60*24)) {
      Serial.print("Uptime (in days): "); Serial.println(startTime/60.0/60.0/24.0,1);
    }
    else if(startTime>(60*60)) {
      Serial.print("Uptime (in hours): "); Serial.println(startTime/60.0/60.0,1);
    }
    else if(startTime>60) {
      Serial.print("Uptime (in minutes): "); Serial.println(startTime/60.0,1);
    }
    else {
      Serial.print("Uptime (in seconds): "); Serial.println(startTime);
    }

    Serial.print("Current Readings: "); 
    Serial.print(lowPowerPanel.getCurrent());
    Serial.print(",");
    Serial.print(solarPanel.getCurrent());
    Serial.print(",");
    Serial.print(inverter.getCurrent());
    Serial.print(",");
    Serial.println(starterBattery.getCurrent());
    
    Serial.println("============================");    
    
    //Serial.print("Battery mV: "); Serial.println(calcBatteryMilliVolts(adcBuffer4.getMedian()));
}

//Update the number of Ah remaining on the battery based on the last minute of current flow
void adjustAh()
{
    //Let's increment/decrement AH of the battery based on amperage flow as an estimate
    mAhFlow=0;
    mAhFlow=mAhFlow+lowPowerPanel.getLastMinuteAvg();
    mAhFlow=mAhFlow+solarPanel.getLastMinuteAvg();
    mAhFlow=mAhFlow+inverter.getLastMinuteAvg();
    mAhFlow=mAhFlow+starterBattery.getLastMinuteAvg();
  
    //If charge, add effeciency
    if(mAhFlow > 0)
      mAhFlow=mAhFlow*(CHARGE_EFFICIENCY * .01);

    //Convert to amp hours
    mAhFlow=mAhFlow/60;
  
    //Ok, update amp hours
    mAhRemaining=mAhRemaining+mAhFlow;  
}

//Returns mV from onboard ADC using a calibrated aref value
long readADC(int pin)
{
  adcBuffer.clear();
  for(int i=0;i<ADC_SAMPLE_SIZE;i++)
  {
    adcBuffer.addValue(analogRead(pin));
  }

  //Aref = 2.522v (calibrated)
  return (adcBuffer.getMedian() * 2522) / 1024 ; // convert readings to mv  
}
  
//Calc battery voltage taking into account the resistor divider
long calcBatteryMilliVolts(long mv)
{
  //voltage divider ratio = 5.137, r2/(r1+r2) where r1=10.33K and r2=2.497K
  //return mv * 5.137;
  return 12960;
}

//Return % state of charge based on battery level
float calcSoC(int mv)
{
  socReset=rtc.now().unixtime();  //used to know how long ago the SoC was last set
  
  //12.4V = 50%, 12:54V = 60%, 12.68V = 70%,  12.82V = 80%, 12.96V = 90%, 13.1V = 100%
  int full=13100;
  int empty=12400;
  float incr=(full-empty)/50.0;  //gives voltage per 1% in mV

  return(((mv-empty)/incr) + 50);
}

//Calc milli amp hours left in battery based on SoC (%)
long calcMilliAmpHoursRemaining(long soc)
{
  //return's 50% of total capacity, as anything below that is not usable
  long ah=AH*(soc*.01);
  return ((ah-(AH/2))*1000);
}

//Calc's how long the battery will last to 50% based on flow in mA passed in
//Use this to calc time left based on any of the inputs, hour, day, etc....
float calcHoursRemaining(long mAflow)
{ 
  //We should now be able to calc hours left
  return ((float)mAhRemaining/mAflow);
}
/*
//print ADC values (more debug than anything else)
void printADC()
{
  //ADC values
  int16_t adc0, adc1, adc2, adc3;
  float amps,volts;
  char adcString[20];
  
  //Show ADC0 reading
  //volts=readADC(0)/1000;
  Serial.print("ADC0 (v): "); Serial.println(volts);
  lcdPrint("ADC0: ");
  dtostrf(volts, 4, 2, adcString); 
  lcdPrint(adcString,0,1);
  delay(1000);
}
  */

//
//RTC functionality
//
// DateTime doc: http://playground.arduino.cc/Code/Time
//
void printCurrentDT()
{
  char dtString[20];
  
  //Grab current datetime
  DateTime now = rtc.now();
    
  //print epoc
  Serial.print("Epoc: ");
  Serial.println(now.unixtime());

  ltoa(now.unixtime(),dtString,10);
  lcdPrint(dtString);
  delay(1000);  
}

//
//LCD functionality
//
void lcdPrint(char *stringToPrint)
{
  lcd.clear();
  lcdPrint(stringToPrint,0,0);
}

void lcdPrint(char *stringToPrint,int col,int line)
{
  lcd.setCursor(col,line);
  lcd.print(stringToPrint);
}

// return values: 0=none, 1=up, 2=down, 3=right, 4=left, 5=select
int lcdReadButton()
{
  int retval=0;

  //Read buttons
  uint8_t buttons = lcd.readButtons();
  
  if (buttons & BUTTON_UP) {
    retval=1;
  }
  if (buttons & BUTTON_DOWN) {
    retval=2;
  }
  if (buttons & BUTTON_LEFT) {
    retval=4;
  }
  if (buttons & BUTTON_RIGHT) {
    retval=3;
  }
  if (buttons & BUTTON_SELECT) {
    retval=5;
  }  

  return retval;
}

int freeRam() 
{
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}

