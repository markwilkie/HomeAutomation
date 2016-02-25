#include "Arduino.h"
#include "pins_arduino.h" 
#include "limits.h"
#include <SPI.h>
//#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"
#include <avr/sleep.h>
#include <avr/power.h>
#include "Debug.h"
#include "RF.h"
#include "Config.h"

//
// Include the correct config values for the unit we're dealing with
//
// 0-Outside
// 1-Laundry - water alarm
// 2-Garage
// 3-Motion
// 4-NA
// 5-Fire
// 6-Front Door (v4.0 board)
// 7-Cabinent Fan
#include "unit1.h"


//
// Flags and counters
//
int sleep_cycles_remaining=0; //var tracking how many sleep cycles we've been through
boolean enableInterruptFlag;  //Turn off/on interruptsn - used to for not having interrupts trigger over and over
int interruptType; //Will be set to -1 for noise, 0 for timer, 1 for interrupt trip, and 2 for interrupt pin release
boolean sentSinceTimerFlag=false;  //Used to determine if we need to send again
int lastRetryCount=0; //Number of retry times on last transmit
int lastGoodTransCount=0; //Number of times we tried to transmit since last success (does not count retry times - that's "one" transmit)
int contextSendCount=0; //Current number for sending context

//
// Definitions
//
RF24 radio(9,10);  // Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins 9 & 10 
typedef enum { wdt_16ms = 0, wdt_32ms, wdt_64ms, wdt_128ms, wdt_250ms, wdt_500ms, wdt_1s, wdt_2s, wdt_4s, wdt_8s } wdt_prescalar_e; //sleep decrarations
byte stateData[STATE_PAYLOADSIZE];
byte contextData[MAX_CONTEXT_PAYLOADSIZE];
byte eventData[EVENT_PAYLOADSIZE];

//Set things up
void setup()
{
    #if defined(ERROR) || defined(VERBOSE)
    Serial.begin(57600);
    printf_begin(); //used by radio.printDetails()
    #endif
    
    //Assign actual functions to function pointers
    #ifdef SETUPFUNC
    setupWorkFunction=SETUPFUNC;
    setupWorkFunction();
    #endif
    #ifdef WORKFUNC
    doWorkFunction=WORKFUNC;
    #endif
    
    //Setup sleep and watchdog if we're sleeping
    #ifndef DELAYTIME
    setup_watchdog(wdt_8s); //wdt_8s
    #endif
    
    //Setup radio
    radio.begin();
    radio.setPALevel(RF24_PA_MAX);          // Higher power level
    radio.setDataRate(RF24_250KBPS);        // Slower datarate for more distance
    radio.setAutoAck(1);                    // Ensure autoACK is enabled
    radio.enableAckPayload();               // Allow optional ack payloads
    radio.enableDynamicPayloads();          // Needed for ACK payload
    radio.setRetries(15,15);                // Smallest time between retries (max is 15 increments of 250us, or 4000us), max no. of retries (15 max)
    
    #if defined(ERROR) || defined(VERBOSE)
    radio.printDetails();                   // Dump the configuration of the rf unit for debugging
    #endif
  
    //Setup pins
    #ifdef LED_PIN
    pinMode(LED_PIN, OUTPUT); 
    #endif
    
    #ifdef REEDPIN
    pinMode(REEDPIN, OUTPUT);
    digitalWrite(REEDPIN, HIGH);      //Power reed if turned on
    #endif
    
    #ifdef POWERPIN
    pinMode(POWERPIN, OUTPUT);
    digitalWrite(POWERPIN, HIGH);      //Power screw contacts if turned on
    #endif    
    
    //Indicate which unit this is
    #ifdef LED_PIN
    blinkLED(5,100);  //get ready for unit blink
    delay(2000);
    blinkLED(UNITNUM,500);
    blinkLED(5,100);  //done
    #endif
    
    //Send context for PI to use upon reset or startup
    #ifdef LED_PIN
    digitalWrite(LED_PIN, HIGH);   
    #endif
    VERBOSE_PRINTLN("Sending Context");
    int payloadLen=buildContextPayload();
    radioSend(contextData,payloadLen); //Send event payload
    #ifdef LED_PIN
    digitalWrite(LED_PIN, LOW);    
    #endif
    
    #ifdef INTERRUPTPIN
    pinMode(INTERRUPTPIN, INPUT_PULLUP);  //pullup is required as LOW is the trigger
    attachInterrupt(INTERRUPTNUM, interruptHandler, LOW);
    #endif 
    
    VERBOSE_PRINTLN("Ready...");
}

void blinkLED(int blinkNum,int delayTime)
{
    #ifdef LED_PIN
    for(int b=0;b<blinkNum;b++)
    {
      digitalWrite(LED_PIN, HIGH);  
      delay(delayTime);
      digitalWrite(LED_PIN, LOW);      
      delay(delayTime);
    }   
    #endif
}
 
//
// Loop infinitely here...
//
void loop()
{
   //Debug output only
   if(interruptType < 0)
     VERBOSE_PRINTLN("Main loop: noise (no meaningful data)");
   if(interruptType == 0)
     VERBOSE_PRINTLN("Main loop: inner loop complete");
   if(interruptType == 1)
     VERBOSE_PRINTLN("Main loop: interrupt");
   if(interruptType == 2)
     VERBOSE_PRINTLN("Main loop: interrupt reset");  
     
   //Do I need to do specialized work?
   #ifdef WORKFUNC
   doWorkFunction();     
   #endif
    
   //Reset flag if timer dings
   if(interruptType == 0)
     sentSinceTimerFlag=false;
     
   //Send Event if necessary
   if(interruptType>0 && (SENDFREQ || !sentSinceTimerFlag)) //woken up based on interrupt
   {
     #ifdef LED_PIN
     digitalWrite(LED_PIN, HIGH);
     #endif
     
     if(EVENTTYPE==2 && ALARMTYPE==0)
       buildAlarmPayload('W','W'); //water    
       
     if(EVENTTYPE==2 && ALARMTYPE==1)
       buildAlarmPayload('F','F'); //fire
     
     if(EVENTTYPE==1)
       buildEventPayload('M','D'); //motion detected, motion detected
  
     if(EVENTTYPE==0)
     {
       if(interruptType == 1) 
         buildEventPayload('O','O'); //opening changed, Opened
       if(interruptType == 2)
         buildEventPayload('O','C'); //opening changed, Closed
     }
     
     VERBOSE_PRINTLN("Sending Event");
     sentSinceTimerFlag=true;
     radioSend(eventData); //Send event payload

     #ifdef LED_PIN
     digitalWrite(LED_PIN, LOW);
     #endif
   }
   
   //Only send state on timer, not interrupt
   if(interruptType==0)
   {
     float temperature;
     byte interruptPinState;
    
     //Gather and send State.  We do this everytime
     #ifdef LED_PIN
     digitalWrite(LED_PIN, HIGH);
     #endif
     float vcc = readVcc(); //Read voltage (battery monitoring)
     #ifdef THERMISTORPIN
     temperature = readTemp();  //Read temperature sensor
     #endif
     #ifdef INTERRUPTPIN
     interruptPinState = digitalRead(INTERRUPTPIN); //read interrupt state   
     #endif
  
     VERBOSE_PRINTLN("Sending State");   
     buildStatePayload(vcc,temperature,interruptPinState); //build state payload
     radioSend(stateData); //Send state payload
     
     //Send context for PI to use upon reset or startup
     contextSendCount++;
     if(contextSendCount > CONTEXT_SEND_FREQ)
     {
       contextSendCount=0;
       VERBOSE_PRINTLN("Sending Context"); 

       int payloadLen=buildContextPayload();
       radioSend(contextData,payloadLen); //Send context
     }     
     
     #ifdef LED_PIN
     digitalWrite(LED_PIN, LOW);
     #endif
   }

   //Inner loop is where we delay or sleep while we wait for interrupts
   startInnerLoop();
}

//Send state
void buildStatePayload(float vcc,float temp,byte interruptPinState)
{
  //
  // Over-the-air packet definition/spec
  //
  // 1 byte:  unit number (uint8)
  // 1 byte:  Payload type (S=state, C=context, E=event
  // 2 bytes: Number of retries on last send (int)
  // 2 bytes: Number of send attempts since last successful send (int)
  // 4 bytes: VCC (float)
  // 4 bytes: Temperature (float)
  // 1 byte:  Pin State (using bits, abcdefgh where 
  //                                 h=interrupt pin (usually reed switch)
  //                                 g=digital signal in from screw terminal
  //                                 f=D7
  //                                 e=D8
  //                                 d=sending interrupt  (h)
  //                                 c=sending digital signal (g)
  //                                 b=sending D7 (f)
  //                                 a=sending D8 (e)
  // 1 byte:  Number (uint8) of uint8 types and uint16 numbers coming (used for extra data from adc or whatever)
  //  ....these two lines for each extra indicated above...
  // 1 byte:  Data Type  (l=long, f=float, a=ascii)
  // 4 bytes: Data
  //
  
  //Set pin states
  uint8_t pinState=B00000000;
  if(TRIGGERTYPE != 0)  //is the interrupt turned on?
  {
      pinState = pinState | B10000000 | (uint8_t)(interruptPinState<<3);
  }
  
  //Create state package
  stateData[0]=(uint8_t)UNITNUM; //unit number
  stateData[1]='S';
  memcpy(&stateData[2],&lastRetryCount,2);  //last retry
  memcpy(&stateData[4],&lastGoodTransCount,2);  //attempts since last successful send
  memcpy(&stateData[6],&vcc,4);  //vcc voltage (mv)
  memcpy(&stateData[10],&temp,4); //temperature (2 decimals implied)
  stateData[14]=pinState;        //see protocol above
  stateData[15]=(uint8_t)0;      //number of option data bits coming
}

//Send event
void buildEventPayload(byte eventCodeType,byte eventCode)
{
  buildAlarmOrEventPayload('E',eventCodeType,eventCode);
}

//Send alarm
void buildAlarmPayload(byte eventCodeType,byte eventCode)
{
    buildAlarmOrEventPayload('A',eventCodeType,eventCode);
}

void buildAlarmOrEventPayload(byte payloadType,byte eventCodeType,byte eventCode)
{
  //
  // Over-the-air packet definition/spec
  //
  // 1 byte:  unit number (uint8)
  // 1 byte:  Payload type (S=state, C=context, E=event, A=alarm 
  // 2 bytes: Number of retries on last send (int)
  // 2 bytes: Number of send attempts since last successful send (int)  
  // 1 byte:  eventCodeType (O-opening change)
  // 1 byte:  eventCode (O-opened, C-closed)
  //
  
  //Create state package
  eventData[0]=(uint8_t)UNITNUM; //unit number
  eventData[1]=payloadType;
  memcpy(&eventData[2],&lastRetryCount,2);  //last retry
  memcpy(&eventData[4],&lastGoodTransCount,2);  //attempts since last successful send
  eventData[6]=eventCodeType;
  eventData[7]=eventCode;
}

//Send context
int buildContextPayload()
{
  //
  // Over-the-air packet definition/spec
  //
  // 1 byte:  unit number (uint8)
  // 1 byte:  Payload type (S=state, C=context, E=event  
  // 1 byte:  location (G=Garage)
  //
  
  //Create context package
  contextData[0]=(uint8_t)UNITNUM; //unit number
  contextData[1]='C';
  memcpy(&contextData[2],UNITDESC,strlen(UNITDESC));
  
  //return length
  return strlen(UNITDESC)+2;
}

//Read voltage to see battery level by using internal 1.1V ref
float readVcc() 
{
  long result;
  // Read 1.1V reference against AVcc
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA,ADSC));
  result = ADCL;
  result |= ADCH<<8;
  result = 1126400L / result; // Back-calculate AVcc in mV
  
  VERBOSE_PRINT("VCC: ");
  VERBOSE_PRINTLN(result/1000.0);

  return result/1000.0;
}


//Read Temperature
#ifdef THERMISTORPIN
float readTemp()
{
  uint8_t i;
  float average;
  static int samples[NUMSAMPLES];
 
  // take N samples in a row, with a slight delay
  for (i=0; i< NUMSAMPLES; i++) {
   samples[i] = analogRead(THERMISTORPIN);
   delay(10);
  }

  // average all the samples out
  average = 0;
  for (i=0; i< NUMSAMPLES; i++) {
     average += samples[i];
  }
  average /= NUMSAMPLES;
 
  //printf("Average analog reading %d\n",(int)average); 
 
  // convert the value to resistance
  average = 1023 / average - 1;
  average = SERIESRESISTOR / average;
 
  float steinhart;
  steinhart = average / THERMISTORNOMINAL;     // (R/Ro)
  steinhart = log(steinhart);                  // ln(R/Ro)
  steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
  steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
  steinhart = 1.0 / steinhart;                 // Invert
  steinhart -= 273.15;                         // convert to C
 
  //Convert to F and return
  float retVal= steinhart*(9.0/5.0)+32.0;
  VERBOSE_PRINT("Temperature: ");
  VERBOSE_PRINTLN(retVal);
  return retVal;
}
#endif

void radioSend(byte *payload)
{
  int payloadLen=0;
  
    //Choose payload size and pipe based on type
  if((byte)payload[1]=='S')
  {
    payloadLen=STATE_PAYLOADSIZE;  
  }
  if((byte)payload[1]=='E')
  {
    payloadLen=EVENT_PAYLOADSIZE;  
  }  
  if((byte)payload[1]=='A')
  {
    payloadLen=EVENT_PAYLOADSIZE;
  }  
  
  radioSend(payload,payloadLen);
}

//Send raw payload
void radioSend(byte *payload,int payloadLen)
{
  byte gotByte;
  
  #ifdef RADIO_OFF
  return;
  #endif
      
  //Set payload size
  radio.setPayloadSize(payloadLen); 
  
  //Choose pipe based on type
  if((byte)payload[1]=='C')
  {
    radio.openWritingPipe(CONTEXT_PIPE);  
  }
  if((byte)payload[1]=='S')
  {
    radio.openWritingPipe(STATE_PIPE);  
  }
  if((byte)payload[1]=='E')
  {
    radio.openWritingPipe(EVENT_PIPE);  
  }  
  if((byte)payload[1]=='A')
  {
    radio.openWritingPipe(ALARM_PIPE);  
  }  
  
  //Turn radio on
  radio.powerUp();  
  delay(50); //settle down  

  //Loop for max retries
  boolean retryFlag=true;
  lastRetryCount=0;
  lastGoodTransCount++; //increment by one in case we're not successful.  Note this is set to zero upon successful send
  while(retryFlag)
  {
    //Write payload out  
    if(radio.write((const void *)payload,radio.getPayloadSize()))
    {
      retryFlag=false; //exit loop because we succeeded
      lastGoodTransCount=0; //since we succeeded, reset this counter to zero
      
      if(!radio.available())
      {                             
        // If nothing in the buffer, we got an ack but it is blank
        VERBOSE_PRINTLN("Got blank response.");     
      }
      else
      {   
    
        while(radio.available() )
        {                      
            // If an ack with payload was received
            radio.read(&gotByte,1);                  // Read it, and display
            VERBOSE_PRINT("Got response: ");
            VERBOSE_PRINTLN(gotByte);
        }
      }
    }
    else
    {
      if(lastRetryCount>=MAXRRETRIES)
      {
        ERROR_PRINTLN("Sending failed.  Not retrying anymore"); 
        retryFlag=false;
      }
      else
      {
        lastRetryCount++;
        VERBOSE_PRINT("Send failed, retry # ");        
        VERBOSE_PRINTLN(lastRetryCount);        
        delay(RETRYDELAY);
      }
    }
  }
  
  //power down radio
  radio.powerDown();
  delay(50); //settle down
}

// 0=16ms, 1=32ms,2=64ms,3=125ms,4=250ms,5=500ms
// 6=1 sec,7=2 sec, 8=4 sec, 9= 8sec
void setup_watchdog(uint8_t prescalar)
{
  prescalar = min(9,prescalar);
  uint8_t wdtcsr = prescalar & 7;
  if ( prescalar & 8 )
    wdtcsr |= _BV(WDP3);

  MCUSR &= ~_BV(WDRF);
  WDTCSR = _BV(WDCE) | _BV(WDE);
  WDTCSR = _BV(WDCE) | wdtcsr | _BV(WDIE);
}

// Watchdog Interrupt Service / is executed when  watchdog timed out
ISR(WDT_vect) 
{
  //--sleep_cycles_remaining;
}

//
// Loop for every 'delay' or 'sleep' period
//
void startInnerLoop()
{
   //reset flags
   //enableInterruptFlag=true;
   interruptType=-1;  //noise - not set
   
   #ifdef INTERRUPTPIN   
   //If interrupt pin is already low (triggered), wait until it's not before sleeping
   //   this is because of noise in certain systems based on the cpu waking up
   int counter=0;
   while(enableInterruptFlag && digitalRead(INTERRUPTPIN) == LOW)
   {
     delay(250);
     counter++;
     if(counter > 50)  //don't lock up
     {
       ERROR_PRINTLN("Interrupt not cleared");
       break;
     }
   }
   #endif
   
   //track state change
   static boolean lastPinState = HIGH;  //LOW triggers, so HIGH will get the ball rolling by being different
   
   //Reset cycles if necessary
   if(sleep_cycles_remaining <= 0)
   {
     sleep_cycles_remaining=SLEEPCYCLES; //setup how many cycles for this inner loop
   }
   
   //Loop for sleep (or delay) cycles
   while(sleep_cycles_remaining)
   {
     //If delay time is specificed, delay instead of sleep.  Useful for doing work that requires board to stay awake (like PWM)
     #ifdef DELAYTIME
     VERBOSE_PRINTLN("Delaying now...");
     delay(DELAYTIME);     
     #else
     VERBOSE_PRINTLN("Going to sleep now...");
     sleepNow(); //Remember, this will only seep for 8 seconds before watchdog timer wakes things up
     #endif

     //We woke up, so decrement sleep cycles
     --sleep_cycles_remaining;
     
     VERBOSE_PRINT("Woke Up - Sleep/Delay Cycles Remaining: ");
     VERBOSE_PRINTLN(sleep_cycles_remaining);
     
     //Check if timer.  If so, set type to 0 and return
     if(sleep_cycles_remaining <= 0)
     {
       interruptType=0;
       VERBOSE_PRINTLN("Inner Loop is done - sleep/delay cycles completed");
       break;
     }
     
     //Do I need to do work in this inner loop?
     #if defined WORKINNERLOOP && defined WORKFUNC
     doWorkFunction();     
     #endif

     #ifdef INTERRUPTPIN     
     //Check if we got woken up because of an interrupt before looping again
     if(TRIGGERTYPE != 0)
     {
       //Check if interrupt is "real" or not  (50ms should be the "floor" for general stability - not sure why, might be just debug print)
       delay(TRIGGERLEN);  //wait for time to be real.  We'll check pin state farther down

       boolean pinState = digitalRead(INTERRUPTPIN);
       if(pinState != lastPinState)  //if different, then we must have got woken up because of the pin interrupt or reset
       {
         //turn off/on interrupt as needed based on pin, not OPENSTATE
         if(pinState == LOW) //LOW triggers hardware interrupt
           enableInterruptFlag=false;  //turn off interrupt until state change so we don't keep interrupting endlessly
         else
           enableInterruptFlag=true;  //turn back on interrupt for next time
         
         lastPinState=pinState;
         if(pinState == LOW)  //must be LOW to trip interrupt
         {
             interruptType=1; //"open" event trigger interrupt
             break; //let's get out of the sleep cycle loop
         }
         else
         {
             if(TRIGGERTYPE == 1) //only check pin reset if interrupt config is both on trigger and release
             {
                 interruptType=2; //"close" event trigger interrupt
                 break; //let's get out of the sleep cycle loop
             }
         }
       }
       VERBOSE_PRINT("Interrupt Type: "); 
       VERBOSE_PRINTLN(interruptType); 
     }
   #endif
   }
}

//Puts things to DEEP sleep and sets the interrupt pin
//
// See http://www.gammon.com.au/forum/?id=11497
//
void sleepNow()
{
  #if defined(ERROR) || defined(VERBOSE)
  delay(100);  //give time for serial to work when printing
  #endif
  
  // disable ADC
  byte old_ADCSRA=ADCSRA;
  ADMUX=0;  //Clear MUX register for deep sleep
  ADCSRA = 0; 
  
  //Set mode and enable sleep bit
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); // sleep mode is set here
  noInterrupts(); 
  sleep_enable();

  // turn off brown-out enable in software
  MCUCR = bit (BODS) | bit (BODSE);  // turn on brown-out enable select
  MCUCR = bit (BODS);        // this must be done within 4 clock cycles of above

  //If we're using them, enable interrupts before we go to sleep
  if(enableInterruptFlag)
    attachInterruptPin(); 
    
  interrupts();
  sleep_cpu();                        // System sleeps here
  sleep_disable();                     // System continues execution here when watchdog timed out or interrupt is tripped
  
  //Turn ADC back on
  ADCSRA = old_ADCSRA;
  
  //Now that we're awake, turn interrupts back off
  if(enableInterruptFlag)
    detachInterruptPin(); 
}

void interruptHandler(void)
{
  //No need to do anything here...
}

void attachInterruptPin()
{
  #ifdef INTERRUPTPIN
  attachInterrupt(INTERRUPTNUM, interruptHandler, LOW);
  delay(100);
  #endif
}

void detachInterruptPin()
{
  #ifdef INTERRUPTPIN
  detachInterrupt(INTERRUPTNUM);
  #endif
}




