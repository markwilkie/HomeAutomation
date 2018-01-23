#include <Wire.h>  

//component specific includes
#include <RTClib.h>
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>

//Config
#include "config.h"

//Utils

#include "PrecADC.h"

//setup
RTC_DS3231 rtc;
Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();

//Get ADC's going
PrecADC lowPowerPanel = PrecADC(0,GAIN_TWOTHIRDS, 2482, .066);
PrecADC solarPanel = PrecADC(1,GAIN_TWOTHIRDS, 2482, .066);
PrecADC inverter = PrecADC(2,GAIN_TWOTHIRDS, 2482, .066);
PrecADC mainBattery = PrecADC(3,GAIN_TWOTHIRDS, 2482, .066);

// These #defines make it easy to set the backlight color on LCD
#define RED 0x1
#define YELLOW 0x3
#define GREEN 0x2
#define TEAL 0x6
#define BLUE 0x4
#define VIOLET 0x5
#define WHITE 0x7

//Gloabls
long startTime;
int hz;
int loopCount;

void setup() 
{
  Serial.begin(57600);

  //setup lcd
  Serial.println("Initializing LCD...");
  lcd.begin(16,2);  //16 x 2 lcd
  lcdPrint("Initializing....");
  //lcd.setBacklight(WHITE);

  //setup MEGA ADC to 2.56 for reading battery voltage
  analogReference(INTERNAL2V56);

  //setup real time clock (RTC)
  rtc.begin();
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, lets set the time!");
    rtc.adjust(0);  //set to ephoch as a baseline
  }  

  //ADC
  lowPowerPanel.begin();
  solarPanel.begin();
  inverter.begin();
  mainBattery.begin();

  //init vars
  startTime = rtc.now().unixtime();
  hz=0;
  loopCount=0;
}

void loop() 
{
  //status vars
  hz++;

  //read adc 
  lowPowerPanel.read(); //samples pin 
  solarPanel.read();
  inverter.read();
  mainBattery.read();  

  //Every second, we'll do work
  if(rtc.now().unixtime() != startTime)
  {    
    //debuf
    loopCount++;
    if(loopCount > 10)
    {
      loopCount=0;
      //printStatus();
    }
    
    //add readings to time based buffers for further reporting
    lowPowerPanel.add(); 
    solarPanel.add();
    inverter.add();
    mainBattery.add();  
    
    //reset vars
    startTime=rtc.now().unixtime();
    hz=0;    
  }
  
  //printCurrentDT();
  //printADC();
  //printVolt();
}

void printStatus()
{
    //print status
    Serial.print("Free Ram: "); Serial.println(freeRam());
    Serial.print("Hz: "); Serial.println(hz);
    Serial.print("Second: "); Serial.println(startTime);

    Serial.print("Readings: "); 
    Serial.print(lowPowerPanel.getCurrent());
    Serial.print(",");
    Serial.print(solarPanel.getCurrent());
    Serial.print(",");
    Serial.print(inverter.getCurrent());
    Serial.print(",");
    Serial.println(mainBattery.getCurrent());
    
    //Serial.println(adcBuffer1.getMedian());
    //Serial.println(adcBuffer2.getMedian());
    //Serial.println(adcBuffer3.getMedian());
    //Serial.print("Battery mV: "); Serial.println(calcBatteryMilliVolts(adcBuffer4.getMedian()));
}


  
//Calc battery voltage taking into account the resistor divider
long calcBatteryMilliVolts(long mv)
{
  //voltage divider ratio = 5.137, r2/(r1+r2) where r1=10.33K and r2=2.497K
  return mv * 5.137;
}

/*
//Returns mV from onboard ADC using a calibrated aref value
long readADC(int pin,int samples)
{
  adcBufferRaw.clear();
  for(int i=0;i<samples;i++)
  {
    adcBufferRaw.addValue(analogRead(pin));
  }

  //Aref = 2.522v (calibrated)
  //voltage divider ratio = 5.137, r2/(r1+r2) where r1=10.33K and r2=2.497K
  return (adcBufferRaw.getMedian() * 2522) / 1024 ; // convert readings to volt  
}
*/


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
    
  amps=readACS712Amps(0);
  Serial.print("ACS712: "); Serial.println(amps);
  lcdPrint("ACS712: ");
  dtostrf(amps, 4, 2, adcString); 
  lcdPrint(adcString,0,1);
  delay(1000);

  //Ok, here's ADC1
  adc1 = ads.readADC_SingleEnded(1);
  Serial.print("AIN1: "); Serial.println(adc1);
  lcdPrint("ADC1: ");
  ltoa(adc1,adcString,10);
  lcdPrint(adcString,0,1);
  delay(1000);
      
  adc2 = ads.readADC_SingleEnded(2);
  Serial.print("AIN2: "); Serial.println(adc2);
  lcdPrint("ADC2: ");
  ltoa(adc2,adcString,10);
  lcdPrint(adcString,0,1);
  delay(1000);
      
  adc3 = ads.readADC_SingleEnded(3);
  Serial.print("AIN3: "); Serial.println(adc3);
  lcdPrint("ADC3: ");
  ltoa(adc3,adcString,10);
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

