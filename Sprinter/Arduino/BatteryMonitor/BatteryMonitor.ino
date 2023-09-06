#include <Arduino.h>
#include <Wire.h>  

//component specific includes
#include <RTClib.h>
#include <EasyTransfer.h>  //https://github.com/madsci1016/Arduino-EasyTransfer

//Helpers
#include "SerialPayload.h"
#include "PrecADCList.h"
#include "Battery.h"
#include "WaterTank.h"
#include "debug.h"

//setup
RTC_DS3231 rtc;

//current sensors
PrecADCList precADCList = PrecADCList();

//Battery and water tank
Battery battery = Battery();
WaterTank waterTank = WaterTank();

//Serial protocol for coms with screen
EasyTransfer scrSerial;
SERIAL_PAYLOAD_STRUCTURE scrPayload;

//For debugging
//#define SIMULATED_CURRENT_VALUES

//Init onboard ADC buffer
#define ADC_SAMPLE_SIZE 10

//General Globals
long bootTime;
long startTime;
long currentTime;
int hz;

void setup() 
{
  //Debug serial
  Serial.begin(115200);
  Serial.println("Initializing...");

  //Screen serial
  Serial1.begin(115200);
  delay(2000);
  scrSerial.begin(details(scrPayload), &Serial1);

  //Pause to catch our breath
  delay(1000);

  //setup MEGA ADC to 2.56 for reading battery voltage
  //analogReference(INTERNAL2V56);

  //setup real time clock (RTC)
  rtc.begin();
  //rtc.adjust(0);  //set to epoch as a baseline

  //Precision ADC circuit buffers
  precADCList.begin();

  //Battery and Water tank
  battery.begin(readVcc(),rtc.now().unixtime());
  waterTank.init();
 
  //init vars
  bootTime = rtc.now().unixtime();
  startTime=bootTime;
  hz=0;

  //Display
  Serial.println("Monitor Started");
  printSetupCommands();
}

void loop() 
{
  //status vars
  static int loophz=0;
  loophz++;

  //check serial input for setup, or lcd button presses
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

    //Update screen
    updateRemoteScreen();    

    //Once a minute, we add/subtract the mA to the current battery charge  (only do this once)
    if(!(currentTime % 60))
    {
      //Get voltage from battery 
      battery.readThenAdd(rtc.now().unixtime());

      //Adjust SoC if appropriate
      long socReset=currentTime-battery.getSoCReset();
      long drainmah=precADCList.getDrainSum(socReset);      
      battery.adjustSoC(rtc.now().unixtime(),drainmah);

      //Adjust Ah left on battery based on last minute mAh flow
      long mAhFlow=precADCList.getLastMinuteAvg();
      battery.adjustAh(mAhFlow);
    }       
  }
}

void updateRemoteScreen()
{
  //Load data structure to send to screen
  #ifndef SIMULATED_CURRENT_VALUES
  updatePayload();
  #else
  updatePayloadDebug();
  #endif

  //Send data
  scrSerial.sendData();
}

void updatePayload()
{
  float drawCurrent=((float)precADCList.getADC(0)->getCurrent())+((float)precADCList.getADC(2)->getCurrent());  //aux and inverter
  float chargeCurrent=((float)precADCList.getADC(1)->getCurrent())+((float)precADCList.getADC(3)->getCurrent());  //solar and alternator

  scrPayload.stateOfCharge=battery.getSoC();
  scrPayload.stateOfWater=waterTank.readLevel();
  scrPayload.volts=battery.getVolts();
  scrPayload.chargeAh=chargeCurrent*.001;
  scrPayload.drawAh=drawCurrent*-.001;
  scrPayload.batteryHoursRem=battery.getHoursRemaining(precADCList.getCurrent());
  scrPayload.waterDaysRem=0;
}

void updatePayloadDebug()
{
  scrPayload.stateOfCharge=random(50)+50;  // 50-->100
  scrPayload.stateOfWater=waterTank.readLevel(); //random(100);
  scrPayload.volts=((double)random(40)+100.0)/10.0;  // 10.0 --> 14.0
  scrPayload.chargeAh=((double)random(150))/10.0;  // 0 --> 15.0
  scrPayload.drawAh=((double)random(250))/10.0;    // 0 --> 25.0
  scrPayload.batteryHoursRem=((double)random(99))/10.0;   // 0 --> 99
  scrPayload.waterDaysRem=((double)random(10))/10.0;   // 0 --> 10
}

void printStatus()
{ 
  char buffer[20];
  long socReset=currentTime-battery.getSoCReset();
  
  //print ADC status
  precADCList.printStatus();    
  
  //print general status
  Serial.println("============================");
  Serial.print("Free Ram: "); Serial.println(freeRam());
  Serial.print("SoC: "); Serial.println(battery.getSoC());
  Serial.print("Voltage: "); Serial.println(battery.getVolts());
  Serial.print("Ah flow: "); Serial.println(precADCList.getCurrent()*.001,3);
  Serial.print("Ah since SoC reset: "); Serial.println(precADCList.getDrainSum(socReset)*.001,3);
  Serial.print("Usable Ah left: "); Serial.println(battery.getAmpHoursRemaining());
  Serial.print("Hours left to full or empty: "); Serial.println(battery.getHoursRemaining(precADCList.getCurrent()));  
  Serial.print("Hz: "); Serial.println(hz);
  Serial.print("Current Second: "); Serial.println(currentTime);
  Serial.print("Since SoC reset: "); Serial.println(buildTimeLabel(socReset,buffer));
  Serial.print("Uptime: "); Serial.println(buildTimeLabel(currentTime-bootTime,buffer)); 
  Serial.print("Duty Cycle: "); Serial.println(battery.getDutyCycle(),1); 
  Serial.print("V Min: "); Serial.println(battery.getVMin(),1); 
  Serial.print("V Max: "); Serial.println(battery.getVMax(),1); 
}

//Pass in your own buffer to fill (at least 20 chars long)
char *buildTimeLabel(long seconds,char *buffer)
{
  double timeVal=0.0;

  if(seconds>(60*60*24))  //days
  {
    timeVal=seconds/60.0/60.0/24.0;
    if(timeVal>=100)
      strcpy(buffer,"D99+");
    else
    {
      buffer[0]='D';
      dtostrf(timeVal, 3, 1, buffer+1);   
    }
  }
  else if(seconds>(60*60)) //hours
  {
    timeVal=seconds/60.0/60.0;
    buffer[0]='H';
    dtostrf(timeVal, 3, 1, buffer+1);    
  }
  else if(seconds>60) //minutes
  {
    timeVal=seconds/60.0;
    buffer[0]='M';
    dtostrf(timeVal, 3, 1, buffer+1);     
  }
  else 
  {
    buffer[0]='S';
    ltoa(seconds, buffer+1, 10);   //seconds
  }

  return buffer;
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
