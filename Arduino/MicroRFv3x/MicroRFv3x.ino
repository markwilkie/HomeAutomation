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
// 2-Weather
// 3-Motion
// 4-Front porch
// 5-Fire
// 6-Front Door (v4.0 board)
// 7-Cabinent Fan
// 8-Garage
#include "unit2.h"


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
volatile int interruptCount=0; //interrupt count

//
// Definitions
//
RF24 radio(9,10);  // Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins 9 & 10 
typedef enum { wdt_16ms = 0, wdt_32ms, wdt_64ms, wdt_128ms, wdt_250ms, wdt_500ms, wdt_1s, wdt_2s, wdt_4s, wdt_8s } wdt_prescalar_e; //sleep decrarations

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

    //Setup power peripherals
    #ifdef POWERPIN
    pinMode(POWERPIN, OUTPUT);
    digitalWrite(POWERPIN, LOW); //default to LOW
    #endif    

    //If POWER_WAKEUP is not defined, we'll leave the peripheral on the whole time
    #if defined(POWERPIN) && !defined(POWER_WAKEUP)
    digitalWrite(POWERPIN, HIGH); //Turn on peripheral power and just leave it on
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
    byte payloadData[MAX_PAYLOADSIZE];
    int payloadLen=buildContextPayload(payloadData,'B');
    radioSend(payloadData,payloadLen); //Send event payload
    #ifdef LED_PIN
    digitalWrite(LED_PIN, LOW);    
    #endif
    
    #ifdef INTERRUPTPIN
    pinMode(INTERRUPTPIN, INPUT_PULLUP);  //pullup is required as LOW is the trigger
    attachInterrupt(digitalPinToInterrupt(INTERRUPTPIN), interruptHandler, LOW);
    #endif 

    //#ifdef INTERRUPTPIN2
    //pinMode(INTERRUPTPIN2, INPUT_PULLUP);  //pullup is required as LOW is the trigger
    //attachInterrupt(digitalPinToInterrupt(INTERRUPTPIN2), interruptHandler2, LOW);
    //#endif     
    
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
   //Buffer for payload
   byte payloadData[MAX_PAYLOADSIZE];

   #ifdef VERBOSE
   //Debug output only
   if(interruptType < 0)
     VERBOSE_PRINTLN("Main loop: noise (no meaningful data)");
   if(interruptType == 0)
     VERBOSE_PRINTLN("Main loop: inner loop complete");
   if(interruptType == 1)
     VERBOSE_PRINTLN("Main loop: interrupt");
   if(interruptType == 2)
     VERBOSE_PRINTLN("Main loop: interrupt reset");  
   #endif

   //Do I need to do specialized work?
   #ifdef WORKFUNC
   delay(500);  //locks up with lower values.  Perhaps I2C stuff??
   doWorkFunction();     
   #endif
   //type 0 means that timer has "dinged"
   //Only send state on timer, not interrupt
   if(interruptType==0 || SEND_STATE_ON_EVENT)
   {
      sentSinceTimerFlag=false;  //reset flag so interrupts will be sent again
      handleState(payloadData);

      #ifdef WEATHER_INSTALLED
      handleWeather(payloadData);
      #endif   
   }

   //Send Event if necessary
   #ifdef INTERRUPTPIN
   if(interruptType>0 && (SENDFREQ || !sentSinceTimerFlag)) //woken up based on interrupt
   {
      handleEvent(payloadData,INTERRUPTPIN);
      interruptCount=0; //reset counter for this interrupt
   }
   #endif 

   //Inner loop is where we delay or sleep while we wait for interrupts
   startInnerLoop();
}

#ifdef INTERRUPTPIN
void handleEvent(byte *payloadData,int _eventType)
{
  int payloadLen=0;

  if(_eventType==3)
     payloadLen=buildEventPayload(payloadData,'C','C'); //counter event
     
  if(_eventType==2 && ALARMTYPE==0)
     payloadLen=buildAlarmPayload(payloadData,'W','W'); //water    
       
  if(_eventType==2 && ALARMTYPE==1)
     payloadLen=buildAlarmPayload(payloadData,'F','F'); //fire
     
  if(_eventType==1)
     payloadLen=buildEventPayload(payloadData,'M','D'); //motion detected, motion detected
  
  if(_eventType==0)
  {
     if(_eventType == 1) 
       payloadLen=buildEventPayload(payloadData,'O','O'); //opening changed, Opened
     if(_eventType == 2)
       payloadLen=buildEventPayload(payloadData,'O','C'); //opening changed, Closed
  }

  #ifdef LED_PIN
  digitalWrite(LED_PIN, HIGH);
  #endif
     
  VERBOSE_PRINTLN("Sending Event");
  sentSinceTimerFlag=true;
  radioSend(payloadData,payloadLen); //Send event payload

  #ifdef LED_PIN
  digitalWrite(LED_PIN, LOW);
  #endif
}
#endif

void handleState(byte* payloadData)
{
    int payloadLen=0;
    float temperature=0.0;
    int adcValue=0;
    byte interruptPinState;

    //Gather and send State.  We do this everytime
    #if defined(POWERPIN) && defined(POWER_WAKEUP)
    digitalWrite(POWERPIN, HIGH);      //Turn on perif so we can do a read
    delay(POWER_WAKEUP); //Wait for things to calm down per datasheet
    #endif  
     
    float vcc = readVcc(); //Read voltage (battery monitoring)
    #ifdef THERMISTORPIN
    temperature = readTemp();  //Read temperature sensor
    #endif
    #ifdef ADC_PIN
    adcValue = readADC(ADC_PIN,vcc);  //Read adc pin
    #endif     
    #ifdef INTERRUPTPIN
    interruptPinState = digitalRead(INTERRUPTPIN); //read interrupt state   
    #endif

    #if defined(POWERPIN) && defined(POWER_WAKEUP)
    digitalWrite(POWERPIN, LOW);      //Turn back off before we transmit
    #endif  

    #ifdef LED_PIN
    digitalWrite(LED_PIN, HIGH);
    #endif
  
    VERBOSE_PRINTLN("Sending State");   
    payloadLen=buildStatePayload(payloadData,vcc,temperature,interruptPinState,adcValue); //build state payload
    radioSend(payloadData,payloadLen); //Send state payload
     
    //Send context for PI to use upon reset or startup
    contextSendCount++;
    if(contextSendCount > CONTEXT_SEND_FREQ)
    {
      contextSendCount=0;
      VERBOSE_PRINTLN("Sending Context"); 

      int payloadLen=buildContextPayload(payloadData,'H');
      radioSend(payloadData,payloadLen); //Send context
    }     
     
    #ifdef LED_PIN
    digitalWrite(LED_PIN, LOW);
    #endif
}

//Send state
int buildStatePayload(byte *payloadData,float vcc,float temp,byte interruptPinState,int adcValue)
{
  //
  // Over-the-air packet definition/spec
  //
  // 1:1 byte:  unit number (uint8)
  // 1:2 byte:  Payload type (S=state, C=context, E=event
  // 2:4 bytes: Number of retries on last send (int)
  // 2:6 bytes: Number of send attempts since last successful send (int)
  // 4:10 bytes: VCC (float)
  // 4:14 bytes: Temperature (float)
  // 1:15 byte:  Presence  ('P' = present, and 'A' = absent)
  // ===========================================
  // 1:16 byte:  Data Type  (a=ADC)
  // 2:18 bytes: int value itself
  // ---
  // 1:16 byte:  Data Type  (w=Weather)
  // 2:18 bytes: Humidity (int)
  
  int payloadLen=0;
  
  //Create state package
  payloadData[0]=(uint8_t)UNITNUM; //unit number
  payloadData[1]='S';
  memcpy(&payloadData[2],&lastRetryCount,2);  //last retry
  memcpy(&payloadData[4],&lastGoodTransCount,2);  //attempts since last successful send
  memcpy(&payloadData[6],&vcc,4);  //vcc voltage (mv)
  memcpy(&payloadData[10],&temp,4); //temperature 
  payloadData[14]=getPresence(adcValue);    //see protocol above
  payloadLen=15; //So far, the len is 15 w/o any optional data

  //Send ADC if I have it
  #ifdef ADC_PIN
  payloadData[15]='a';
  memcpy(&payloadData[16],&adcValue,2); //ADC int value
  payloadLen=18;  //Since we've added ADC, we need to increase the len
  #endif    

  //Send WEATHER stuff if I have it
  #ifdef WEATHER_INSTALLED
  payloadData[15]='w';
  memcpy(&payloadData[10],&(aReading.temperature),4); //temperature from sensor
  
  int intHum=(int)aReading.humidity;
  memcpy(&payloadData[16],&intHum,2); //Humidity value
  payloadLen=18;  //Since we've added the weather info
  #endif

  return payloadLen;
}

byte getPresence(int adcValue)
{
  char presence = '-';
  
  //If I have a ADC pin defined, determine if past threshold
  #ifdef ADC_THRESHOLD
  if(adcValue >= ADC_THRESHOLD)
  {
    if(ADC_PRESENT)
      presence = 'P';
    else
      presence = 'A';
  }
  else
  {
    if(ADC_PRESENT)
      presence = 'A';
    else
      presence = 'P';
  }
  #endif   

  
  return presence;
}

//Send event
int buildEventPayload(byte *payloadData,byte eventCodeType,byte eventCode)
{
  return buildAlarmOrEventPayload(payloadData,'E',eventCodeType,eventCode);
}

//Send alarm
int buildAlarmPayload(byte *payloadData,byte eventCodeType,byte eventCode)
{
    return buildAlarmOrEventPayload(payloadData,'A',eventCodeType,eventCode);
}

int buildAlarmOrEventPayload(byte *payloadData,byte payloadType,byte eventCodeType,byte eventCode)
{
  //
  // Over-the-air packet definition/spec
  //
  // 1:1 byte:  unit number (uint8)
  // 1:2 byte:  Payload type (S=state, C=context, E=event, A=alarm 
  // 2:4 bytes: Number of retries on last send (int)
  // 2:6 bytes: Number of send attempts since last successful send (int)  
  // 1:7 byte:  eventCodeType (O-opening change, C-counter)
  // 1:8 byte:  eventCode (O-opened, C-closed)
  //
  
  int payloadLen=0;
  
  //Create state package
  payloadData[0]=(uint8_t)UNITNUM; //unit number
  payloadData[1]=payloadType;
  memcpy(&payloadData[2],&lastRetryCount,2);  //last retry
  memcpy(&payloadData[4],&lastGoodTransCount,2);  //attempts since last successful send
  payloadData[6]=eventCodeType;
  payloadData[7]=eventCode;
  payloadLen=8;

  VERBOSE_PRINT("Event: ");
  VERBOSE_PRINT(payloadType);
  VERBOSE_PRINT(" ");
  VERBOSE_PRINT(eventCodeType);
  VERBOSE_PRINT(" ");
  VERBOSE_PRINTLN(eventCode);

  return payloadLen;
}

//Send context
int buildContextPayload(byte *payloadData,char tag)
{
  //
  // Over-the-air packet definition/spec
  //
  // 1:1 byte:  unit number (uint8)
  // 1:2 byte:  Payload type (S=state, C=context, E=event  
  // 1:3 byte:  B-boot, or H-heartbeat
  // var byte:  Description
  //
  
  //Create context package
  payloadData[0]=(uint8_t)UNITNUM; //unit number
  payloadData[1]='C';
  payloadData[2]=tag; 
  payloadData[3]='-';
  memcpy(&payloadData[4],UNITDESC,strlen(UNITDESC));
  
  //return length
  return strlen(UNITDESC)+4;
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


#ifdef ADC_PIN
//Read adc pin
int readADC(int adcPin,float vcc) 
{
  delay(100);  //Give a chance for things to calm down since they've most likely just been powered on
  
  long valueSum=0;
  for(int i=0;i<ADCREAD;i++)
  {
    valueSum=valueSum+analogRead(adcPin);
    delay(5);
  }

  int adcAvg=valueSum/ADCREAD;
  float ratio=3.3/vcc;  //3.3 comes from the "target" voltage so the adc reading can be consistent as the battery runs down.
  float adcValue=(float)adcAvg*ratio;
  
  VERBOSE_PRINT("ADC: ");
  VERBOSE_PRINTLN((int)adcValue);  
     
  return (int)adcValue;
}
#endif


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
  if((byte)payload[1]=='S' || (byte)payload[1]=='W')  //state OR weather
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
   interruptType=-1;  //noise - not set

   //
   // only done for the first interrupt pin
   //
   //If interrupt pin is already low (triggered), wait until it's not before sleeping
   //   this is because of noise in certain systems based on the cpu waking up
   #ifdef INTERRUPTPIN   
   int counter=0;
   while(enableInterruptFlag && digitalRead(INTERRUPTPIN) == LOW)
   {
     delay(250);
     counter++;
     if(counter > 50)  //don't lock up
     {
       ERROR_PRINTLN("Interrupt 1 not cleared");
       break;
     }
   }
   #endif

   //track state change
   static boolean lastPinState = HIGH;  //LOW triggers, so HIGH will get the ball rolling by being different
   
   //Loop for sleep (or delay) cycles
   sleep_cycles_remaining=SLEEPCYCLES;
   while(sleep_cycles_remaining)  //We're not using for-next because we're breaking out of the while once we know what kind of interrupt
   {
     //If delay time is specificed, delay instead of sleep.  Useful for doing work that requires board to stay awake all the time (like PWM)
     //  NOT battery friendly to do this of course
     #ifdef DELAYTIME
     VERBOSE_PRINTLN("Delaying now...");
     delay(DELAYTIME);     
     #else
     VERBOSE_PRINTLN("Going to sleep now...");
     sleepNow(); //Remember, this will only seep for 8 seconds before watchdog timer wakes things up and the function exits here
     #endif

     //We woke up, so decrement sleep cycles
     --sleep_cycles_remaining;
     
     VERBOSE_PRINT("Woke Up - Sleep/Delay Cycles Remaining: ");
     VERBOSE_PRINTLN(sleep_cycles_remaining);
     
     //Check if timer was what woke me up.  If so, set type to 0 and return
     if(sleep_cycles_remaining <= 0)
     {
       interruptType=0;  //means timer woke me up, not interrupt
       VERBOSE_PRINTLN("Inner Loop is done - sleep/delay cycles completed");
       break;
     }
     
     //Do I need to do work in this inner loop?
     #if defined WORKINNERLOOP && defined WORKFUNC
     doWorkFunction();     
     #endif

     #ifdef INTERRUPTPIN
     if(handleInterruptState(INTERRUPTPIN,TRIGGERTYPE,&lastPinState))
        break;
     #endif
   }
}

//returns true if break needed
boolean handleInterruptState(int _interruptPin,int _triggerType,boolean *_lastPinState)
{
     //Check if we got woken up because of an interrupt before looping again
     if(_triggerType > 0 && _triggerType < 3)  //not timer or counter only
     {
       //Check if interrupt is "real" or not  (50ms should be the "floor" for general stability - not sure why, might be just debug print)
       delay(TRIGGERLEN);  //wait for time to be real.  We'll check pin next

       boolean pinState = digitalRead(_interruptPin);  //OK, check state of pin NOW, don't trust the interrupt only
       if(pinState != (*_lastPinState))  //if - and only if - pin state is different, then we must have got woken up because of the pin interrupt or reset
       {
         //set last pinstate so we can compare next time 
         (*_lastPinState)=pinState;

         //must be LOW to trip interrupt on board.  HIGH is simply polling every watchdog timer ding
         if(pinState == LOW)  
         {
             //turn off interrupt until state change so we don't keep interrupting endlessly.  This means we'll sleep for 8 seconds before checking again.
             enableInterruptFlag=false;  

             //"open" event trigger interrupt.  "open" is just the first trigger...the 2nd (opposite one) is "closed"
             interruptType=1; 
             return true; //let's get out of the sleep cycle loop
         }
         else
         {
             //turn interrupts back on for next time 
             enableInterruptFlag=true;  
             
             if(_triggerType == 1) //only check pin reset if interrupt config is both on trigger and release
             {
                 interruptType=2; //"close" event trigger interrupt
                 return true; //let's get out of the sleep cycle loop
             }
         }
       }
     }

     return false;
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
 static unsigned long last_interrupt_time = 0;
 unsigned long interrupt_time = millis();
 if (interrupt_time - last_interrupt_time > 50) 
 {
  interruptCount++;
 }
 last_interrupt_time = interrupt_time;  
}

void attachInterruptPin()
{
  #ifdef INTERRUPTPIN
  attachInterrupt(digitalPinToInterrupt(INTERRUPTPIN), interruptHandler, LOW);
  delay(100);
  #endif
}

void detachInterruptPin()
{
  #ifdef INTERRUPTPIN
  detachInterrupt(digitalPinToInterrupt(INTERRUPTPIN));
  #endif
}




