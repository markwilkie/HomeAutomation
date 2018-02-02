#include <Wire.h>  

//component specific includes
#include <RTClib.h>
#include <Adafruit_ADS1015.h>
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>

//Utils
#include "ADC.h"
#include "FastRunningMedian.h"
#include <CircularBuffer.h>   //https://github.com/rlogiacco/CircularBuffer

//Config
#include "config.h"

//setup
RTC_DS3231 rtc;
Adafruit_ADS1115 ads;
Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();

// These #defines make it easy to set the backlight color on LCD
#define RED 0x1
#define YELLOW 0x3
#define GREEN 0x2
#define TEAL 0x6
#define BLUE 0x4
#define VIOLET 0x5
#define WHITE 0x7

//buffers for getting median on ADC reads
#define SAMPLE_SIZE 5
#define BUFFER_SIZE 10 //approx 2 seconds worth at 5+ hertz
FastRunningMedian<long,SAMPLE_SIZE, 0> adcBufferRaw;  //used by each read to store raw result (cleared each time)
FastRunningMedian<long,BUFFER_SIZE, 0> adcBuffer0; //adc0 - mV from WCS1800, low amp panel
FastRunningMedian<long,BUFFER_SIZE, 0> adcBuffer1; //adc1 - mV from ACS712, solar panel
FastRunningMedian<long,BUFFER_SIZE, 0> adcBuffer2;
FastRunningMedian<long,BUFFER_SIZE, 0> adcBuffer3;
FastRunningMedian<long,BUFFER_SIZE, 0> adcBuffer4; //A0 - mV from divider for main battery 

//buffers for over time
CircularBuffer<long> secondBuf0 = CircularBuffer<long>(60);
//CircularBuffer<long, 60> secondBuf1;
//CircularBuffer<long, 60> secondBuf2;
//CircularBuffer<long, 60> secondBuf3;

CircularBuffer<long> minuteBuf0 = CircularBuffer<long>(60);
//CircularBuffer<long, 60> minuteBuf1;
//CircularBuffer<long, 60> minuteBuf2;
//CircularBuffer<long, 60> minuteBuf3;

CircularBuffer<long> hourBuf0  = CircularBuffer<long>(24);
//CircularBuffer<long, 24> hourBuf1;
//CircularBuffer<long, 24> hourBuf2;
//CircularBuffer<long, 24> hourBuf3;

CircularBuffer<long> dayBuf0  = CircularBuffer<long>(30);
//CircularBuffer<long, 30> dayBuf1;
//CircularBuffer<long, 30> dayBuf2;
//CircularBuffer<long, 30> dayBuf3;

//Gloabls
long startTime;
int hz;
int currentSecond;
int currentMinute;
int currentHour;

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

  //setup ADS1115 ADC                                                            -------
  ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit = 0.1875mV (default)
  // ads.setGain(GAIN_ONE);        // 1x gain   +/- 4.096V  1 bit = 0.125mV
  // ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit = 0.0625mV
  // ads.setGain(GAIN_FOUR);       // 4x gain   +/- 1.024V  1 bit = 0.03125mV
  // ads.setGain(GAIN_EIGHT);      // 8x gain   +/- 0.512V  1 bit = 0.015625mV
  // ads.setGain(GAIN_SIXTEEN);    // 16x gain  +/- 0.256V  1 bit = 0.0078125mV  
  ads.begin();  

  //setup real time clock (RTC)
  rtc.begin();
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, lets set the time!");
    rtc.adjust(0);  //set to ephoch as a baseline
  }  

  //init vars
  startTime = rtc.now().unixtime();
  hz=0;
  int currentSecond=0;
  int currentMinute=0;
  int currentHour=0;  
}

void loop() 
{
  //status vars
  hz++;

  //read adc buffers
  adcBuffer0.addValue(readADS1115ADC(0,SAMPLE_SIZE)); 
  adcBuffer1.addValue(readADS1115ADC(1,SAMPLE_SIZE));
  adcBuffer2.addValue(readADS1115ADC(2,SAMPLE_SIZE));
  adcBuffer3.addValue(readADS1115ADC(3,SAMPLE_SIZE));
  adcBuffer4.addValue(readADC(A0,SAMPLE_SIZE));

  //Every second, we'll do work
  if(rtc.now().unixtime() != startTime)
  {    
    //load second buffer every time and increment the counter
    currentSecond++;
    secondBuf0.push(calcWCS1800MilliAmps(adcBuffer0.getMedian()));

    //Time to add to minute?
    if(currentSecond>59)
    {
      printStatus();
      
      currentSecond=0;
      currentMinute++;
      Serial.println("\n\nAdding to minute buffer: ");

      //add average to minute buffer
      minuteBuf0.push(calcAvgFromBuffer(&secondBuf0));

      //Time to add to hour?
      if(currentMinute>59)
      {
        currentMinute=0;
        currentHour++;
        Serial.println("\n\nAdding to hour buffer");

        //Add average
        int avg=0;
        for(int i=0;i<minuteBuf0.size();i++)
          avg=avg+minuteBuf0[i];
        hourBuf0.push(avg/(minuteBuf0.size()));        

        //Time to add to day?
        if(currentHour>23)
        {
          currentHour=0;
          Serial.println("\n\nAdding to day buffer");

          //Add average
          avg=0;
          for(int i=0;i<hourBuf0.size();i++)
            avg=avg+hourBuf0[i];
          dayBuf0.push(avg/(hourBuf0.size()));            
        }
      }
    }
    
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
    Serial.print("mA: "); Serial.println(calcWCS1800MilliAmps(adcBuffer0.getMedian()));
    Serial.println(adcBuffer1.getMedian());
    Serial.println(adcBuffer2.getMedian());
    Serial.println(adcBuffer3.getMedian());
    Serial.print("Battery mV: "); Serial.println(calcBatteryMilliVolts(adcBuffer4.getMedian()));
}

long calcAvgFromBuffer(CircularBuffer<long> *circBuffer)
{
  long avg=0;
  for(int i=0;i<circBuffer->size();i++)
    avg=avg+(*circBuffer)[i];
    
  avg = avg/(circBuffer->size());
  Serial.print("Buffer Avg: "); Serial.println(avg);

  return avg;
}
  
//Calc battery voltage taking into account the resistor divider
long calcBatteryMilliVolts(long mv)
{
  //voltage divider ratio = 5.137, r2/(r1+r2) where r1=10.33K and r2=2.497K
  return mv * 5.137;
}


//Convert mV to milli-amps
long calcWCS1800MilliAmps(long mv)
{
  //2482 calibration
  return ((mv - 2482) / .066);  //2483 is calibrated offset in mV, and 66 is accuracy of device
}

//Convert mV to milli-amps
long calcACS712MilliAmps(long mv)
{ 
  return ((mv - 2472) / .066);  //2472 is calibrated offset in mV, and 66 is accuracy of device
}

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

//Returns mV from the ADS1115 using the default gain (.1875)
long readADS1115ADC(int adcNum,int samples)
{
  ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit = 0.1875mV (default)  
  adcBufferRaw.clear();
  for(int i=0;i<samples;i++)
  {
    adcBufferRaw.addValue(ads.readADC_SingleEnded(adcNum));
  }

  //Serial.println(adcBufferRaw.getMedian());
  return adcBufferRaw.getMedian() * .1875;
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

