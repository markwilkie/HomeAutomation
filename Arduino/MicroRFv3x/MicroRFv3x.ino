#include "Arduino.h"
#include "pins_arduino.h" 
#include "limits.h"
#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"
#include <avr/sleep.h>
#include <avr/power.h>

//
// Include the correct config values for the unit we're dealing with
//
#include "unit2.h"

//
// Flags and counters
//
int sleep_cycles_remaining; //var tracking how many sleep cycles we've been through
boolean enableInterruptFlag;  //Turn off/on interruptsn - used to for not having interrupts trigger over and over
int interruptType=-1; //Set to 0 for timer, 1 for interrupt trip, and 2 for interrupt pin release

//
// Definitions
//
RF24 radio(9,10);  // Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins 9 & 10 
typedef enum { wdt_16ms = 0, wdt_32ms, wdt_64ms, wdt_128ms, wdt_250ms, wdt_500ms, wdt_1s, wdt_2s, wdt_4s, wdt_8s } wdt_prescalar_e; //sleep decrarations
byte stateData[STATE_PAYLOADSIZE];
byte contextData[CONTEXT_PAYLOADSIZE];
byte eventData[EVENT_PAYLOADSIZE];

//Set things up
void setup()
{
    Serial.begin(57600);
    printf_begin(); //used by radio.printDetails()
    
    //Setup radio
    radio.begin();
    radio.setAutoAck(1);                    // Ensure autoACK is enabled
    radio.enableAckPayload();               // Allow optional ack payloads
    radio.enableDynamicPayloads();          // Needed for ACK payload
    radio.setRetries(15,15);                // Smallest time between retries (max is 15 increments of 250us, or 4000us), max no. of retries (15 max)
    radio.printDetails();                   // Dump the configuration of the rf unit for debugging
  
    //Setup sleep and watchdog
    setup_watchdog(wdt_8s); //wdt_8s
    
    //Setup pins
    pinMode(LED_PIN, OUTPUT);  
    pinMode(INTERRUPTPIN, INPUT_PULLUP);  //pullup is required as LOW is the trigger
    pinMode(REEDPIN, OUTPUT);
    
    //Power reed if turned on
    if(TRIGGERTYPE != 0)  
    {
      digitalWrite(REEDPIN, HIGH);
    }
    
    //Send context for PI to use upon reset or startup
    digitalWrite(LED_PIN, HIGH);   
    buildContextPayload('G');
    radioSend(contextData); //Send event payload
    digitalWrite(LED_PIN, LOW);    
}
 
//Get ADC readings and send them out via transmitter 
void loop()
{
   //Debug only
   if(interruptType == 0)
     Serial.println("timer dinged");
   if(interruptType == 1)
     Serial.println("interrupt");
   if(interruptType == 2)
     Serial.println("interrupt reset");    
     
   //Send Event if necessary
   if(interruptType>0) //woken up based on interrupt
   {
     digitalWrite(LED_PIN, HIGH);   
     if(interruptType == 1) 
       buildEventPayload('O','O'); //opening changed, Opened
     if(interruptType == 2)
       buildEventPayload('O','C'); //opening changed, Closed

     radioSend(eventData); //Send event payload
     digitalWrite(LED_PIN, LOW);
   }
   
   //Gather and send State.  We do this everytime
   digitalWrite(LED_PIN, HIGH);
   float vcc = readVcc(); //Read voltage (battery monitoring)
   float temperature = readTemp(THERMISTORPIN);  //Read temperature sensor
   byte interruptPinState = digitalRead(INTERRUPTPIN); //read interrupt state   
   buildStatePayload(vcc,temperature,interruptPinState); //build state payload
   radioSend(stateData); //Send state payload
   digitalWrite(LED_PIN, LOW);

   //Go to low power state and wait for timer and/or interrupt
   sleep();
}

//Send state
void buildStatePayload(float vcc,float temp,byte interruptPinState)
{
  //
  // Over-the-air packet definition/spec
  //
  // 1 byte:  unit number (uint8)
  // 1 byte:  Payload type (S=state, C=context, E=event
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
  memcpy(&stateData[2],&vcc,4);  //vcc voltage (mv)
  memcpy(&stateData[6],&temp,4); //temperature (2 decimals implied)
  stateData[10]=pinState;        //see protocol above
  stateData[11]=(uint8_t)0;      //number of option data bits coming
}

//Send event
void buildEventPayload(byte eventCodeType,byte eventCode)
{
  //
  // Over-the-air packet definition/spec
  //
  // 1 byte:  unit number (uint8)
  // 1 byte:  Payload type (S=state, C=context, E=event  
  // 1 byte:  eventCodeType (O-opening change)
  // 1 byte:  eventCode (O-opened, C-closed)
  //
  
  //Create state package
  eventData[0]=(uint8_t)UNITNUM; //unit number
  eventData[1]='E';
  eventData[2]=eventCodeType;
  eventData[3]=eventCode;
}

//Send context
void buildContextPayload(byte location)
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
  contextData[2]=location;
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

  return result/1000.0;
}

//Read Temperature
float readTemp(int samplePin)
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
  return steinhart*(9.0/5.0)+32.0;;
}

//Send raw payload
void radioSend(byte *payload)
{
  byte gotByte;
      
  //Write the payload
  //radio.stopListening();  
  
  //Choose payload size and pipe based on type
  if((byte)payload[1]=='C')
  {
    radio.setPayloadSize(CONTEXT_PAYLOADSIZE);  
    radio.openWritingPipe(CONTEXT_PIPE);  
  }
  if((byte)payload[1]=='S')
  {
    radio.setPayloadSize(STATE_PAYLOADSIZE);  
    radio.openWritingPipe(STATE_PIPE);  
  }
  if((byte)payload[1]=='E')
  {
    radio.setPayloadSize(EVENT_PAYLOADSIZE);  
    radio.openWritingPipe(EVENT_PIPE);  
  }  
  
  //Turn radio on
  radio.powerUp();  
  delay(50); //settle down  

  //Write payload out  
  if(radio.write((const void *)payload,radio.getPayloadSize()))
  {
    if(!radio.available())
    {                             
      // If nothing in the buffer, we got an ack but it is blank
      printf("Got blank response.\n");     
    }
    else
    {   
  
      while(radio.available() )
      {                      
          // If an ack with payload was received
          radio.read(&gotByte,1);                  // Read it, and display
          printf("Got response %d\n\r",gotByte);
      }
    }
  }
  else
  {
    printf("Sending failed\n");
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
  --sleep_cycles_remaining;
}

void sleep()
{
   //reset flags
   enableInterruptFlag=true;
   interruptType=-1;
  
   //track state change
   static boolean lastPinState = HIGH;  //LOW triggers, so HIGH will get the ball rolling by being different
   
   sleep_cycles_remaining=SLEEPCYCLES; //setup how cycles
   while(sleep_cycles_remaining)
   {
     sleepNow(); //Remember, this will only seep for 8 seconds
     delay(50);  //again, this seems to be needed for stability - not entirely sure why
     
     //Set interrupt type as timer, knowing that pin interrupt will override if necessary
     interruptType=0;
     
     //Check if we got woken up because of an interrupt before looping again
     if(TRIGGERTYPE != 0)
     {
       boolean pinState = digitalRead(INTERRUPTPIN);
       if(pinState != lastPinState)  //if different, then we must have got woken up because of the pin interrupt or reset
       {
         //turn off/on interrupt as needed based on pin, not OPENSTATE
         if(pinState == LOW) //LOW triggers hardware interrupt
           enableInterruptFlag=false;  //turn off interrupt until state change so we don't keep interrupting endlessly
         else
           enableInterruptFlag=true;  //turn back on interrupt for next time
         
         lastPinState=pinState;
         if(pinState == OPENSTATE)
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
     }
   }
}

//Puts things to sleep and sets the interrupt pin
void sleepNow()
{
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); // sleep mode is set here
  sleep_enable();
  if(enableInterruptFlag)
    attachInterrupt();                   // Attach interrupt
  sleep_mode();                        // System sleeps here
  sleep_disable();                     // System continues execution here when watchdog timed out or interrupt is tripped
  if(enableInterruptFlag)
    detachInterrupt();                   // Tear down interrupt
}

void attachInterrupt()
{
    attachInterrupt(INTERRUPTNUM, interruptHandler, LOW);
    delay(100);   
}

void detachInterrupt()
{
    detachInterrupt(INTERRUPTNUM);
}

void interruptHandler(void)
{
  //No need to do anything here...
}



