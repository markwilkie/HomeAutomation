#include <Wire.h>  

//component specific includes
#include <RTClib.h>
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>

//Helpers
#include "PrecADCList.h"
#include "LcdScreens.h"
#include "Battery.h"

//setup
RTC_DS3231 rtc;
Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();
PrecADCList precADCList = PrecADCList();

//Battery
Battery battery = Battery();

//Init onboard ADC buffer
#define ADC_SAMPLE_SIZE 10
FastRunningMedian<long,ADC_SAMPLE_SIZE,0> adcBuffer;

//Thermistor (thermometer)
#define THERMISTORPIN A1   // which analog pin to connect      
#define THERMISTORNOMINAL 10000      // resistance at 25 degrees C  (NTC 104=100K and 103=10K)
#define TEMPERATURENOMINAL 25   // temp. for nominal resistance (almost always 25 C)
#define BCOEFFICIENT 4000 //4038 // The beta coefficient of the thermistor (usually 3000-4000)
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

  //Battery
  battery.begin(readVcc());
 
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
      battery.readThenAdd();

      //Adjust SoC if appropriate
      long socReset=currentTime-battery.getSoCReset();
      long drainmah=precADCList.getDrainSum(socReset);      
      battery.adjustSoC(rtc.now().unixtime(),temperatureRead(),drainmah);

      //Adjust Ah left on battery based on last minute mAh flow
      long mAhFlow=precADCList.getLastMinuteAvg();
      battery.adjustAh(mAhFlow);
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
  Serial.print("SoC: "); Serial.println(battery.getSoC());
  Serial.print("Voltage: "); Serial.println(battery.getVolts());
  Serial.print("Temperature: "); Serial.println(temperatureRead());
  Serial.print("Ah flow: "); Serial.println(precADCList.getCurrent()*.001,3);
  Serial.print("Usable Ah left: "); Serial.println(battery.getAmpHoursRemaining());
  Serial.print("Hours left to 100% or 50%: "); Serial.println(battery.getHoursRemaining(precADCList.getCurrent()));  
  Serial.print("Hz: "); Serial.println(hz);
  Serial.print("Current Second: "); Serial.println(currentTime);
  Serial.print("Since SoC reset: "); Serial.println(buildTimeLabel(currentTime-battery.getSoCReset(),buffer));
  Serial.print("Uptime: "); Serial.println(buildTimeLabel(startTime,buffer)); 
  Serial.print("Duty Cycle: "); Serial.println(battery.getDutyCycle(),3); 
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

int freeRam() 
{
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}

long readVcc() {
  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
  #if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
    ADMUX = _BV(MUX5) | _BV(MUX0);
  #elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
    ADMUX = _BV(MUX3) | _BV(MUX2);
  #else
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #endif  

  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA,ADSC)); // measuring

  uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH  
  uint8_t high = ADCH; // unlocks both

  long result = (high<<8) | low;

  result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  return result; // Vcc in millivolts
}

