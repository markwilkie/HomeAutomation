#include "Arduino.h"
#include "pins_arduino.h" 
#include "limits.h"
#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"
#include <avr/sleep.h>
#include <avr/power.h>

//unit number when there are multiple temp sensors
//
// 0-Outside
// 1-Inside
// 2-Garage
#define UNITNUM 2   //This one will need to change for each physical Arduino

//
// Pin Assignments
//
#define LED_PIN 4     // LED pin (turns on when reading/transmitting)
#define THERMISTORPIN A1  // which analog pin for reading thermistor
#define REEDPIN 3  // which pin powers the reed switch 
#define INTERRUPTPIN 2 //Interrupt pin  (interrupt 0 is pin 2, interrupt 1 is pin 3)
#define INTERRUPTNUM 0 //Interrupt number  (make sure matches interrupt pin)

//
// Config
//
#define SLEEPCYCLES 75  //Sleep cycles wanted  (75 is 10 min assuming 8s timer)
#define TRIGGERTYPE 1   //0- Interrupt is off, 1 - will trigger for both initial trigger of interrupt, AND release.  2 - only on initial interrupt
#define OPENSTATE LOW //set pin (interrupt) state for what defines "open".  LOW is triggered
#define PAYLOADSIZE 6 //Size of package we're sending over the wire

//
// Termistor setup
//
#define THERMISTORNOMINAL 10000    // resistance at 25 degrees C
#define TEMPERATURENOMINAL 25   // temp. for nominal resistance (almost always 25 C)
#define NUMSAMPLES 10  // how many samples to take and average, more takes longer but is more 'smooth'
#define BCOEFFICIENT 3610 //For part number NTCS0603E3103HMT  - he beta coefficient of the thermistor (usually 3000-4000)
#define SERIESRESISTOR 10000  // the value of the 'other' resistor   

//
// Definitions
//
RF24 radio(9,10);  // Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins 9 & 10 
typedef enum { wdt_16ms = 0, wdt_32ms, wdt_64ms, wdt_128ms, wdt_250ms, wdt_500ms, wdt_1s, wdt_2s, wdt_4s, wdt_8s } wdt_prescalar_e; //sleep decrarations
byte data[6]; //payload

//
// Flags and counters
//
int sleep_cycles_remaining; //var tracking how many sleep cycles we've been through
boolean enableInterruptFlag;  //Turn off/on interruptsn - used to for not having interrupts trigger over and over
boolean interruptType=-1; //Set to 0 for timer, 1 for interrupt trip, and 2 for interrupt pin release

//Set things up
void setup()
{
    Serial.begin(57600);
    printf_begin(); //used by radio.printDetails()
    
    //Setup radio
    radio.begin();
    radio.setAutoAck(1);                    // Ensure autoACK is enabled
    radio.enableAckPayload();               // Allow optional ack payloads
    radio.setRetries(0,5);                  // Smallest time between retries, max no. of retries
    radio.setPayloadSize(PAYLOADSIZE);      // Here we are sending 5-byte payloads 
    radio.openWritingPipe(0xF0F0F0F0E3LL);  // Write to "Temperature" pipe
    radio.startListening();                 // Start listening
    radio.powerUp();
    radio.printDetails();                   // Dump the configuration of the rf unit for debugging
  
    //Setup sleep and watchdog
    setup_watchdog(wdt_8s); //wdt_8s
    
    //Setup pins
    pinMode(LED_PIN, OUTPUT);  
    pinMode(INTERRUPTPIN, INPUT_PULLUP);  //pullup is required as LOW is the trigger
    pinMode(REEDPIN, OUTPUT);
    
    //Power reed
    digitalWrite(REEDPIN, HIGH);
}
 
//Get ADC readings and send them out via transmitter 
void loop()
{
   //turn on LED because we're about to read and transmit
   digitalWrite(LED_PIN, HIGH);
   
   float temperature = readTemp(THERMISTORPIN);  //Read temperature sensor
   byte interruptPinState = digitalRead(INTERRUPTPIN); //read interrupt state
   buildPayload(temperature,interruptPinState);
   radioSend(data,6); //Send payload
   
   digitalWrite(LED_PIN, LOW);
   
   if(interruptType == 0)
     Serial.println("timer dinged");
   if(interruptType == 1)
     Serial.println("interrupt");
   if(interruptType == 2)
     Serial.println("interrupt reset");     

   //power down radio as we get ready to sleep  
   radio.powerDown();
   
   //Wait for a minute (not sure why, but the timers/interrupts are unstable if not here)
   delay(50);

   //Go to low power state and wait for timer and/or interrupt
   sleep();
     
   //Now that we're awake, turn the radio back on
   radio.powerUp();
}

//Send Temperature
void buildPayload(float temp,byte interruptPinState)
{
  //Determine if open or closed
  byte isOpen;
  if(OPENSTATE == interruptPinState)
  {
    isOpen=1;
  }
  else
  {
    isOpen=0;
  }
  
  Serial.print("isOpen: ");
  Serial.println(isOpen);
  
  data[0]=(byte)UNITNUM;
  memcpy(&data[1],&temp,4);
  data[5]=isOpen;
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
 
  //printf("Temperature: %d\n",(int)steinhart);
 
  return steinhart;
}

//Send raw payload
void radioSend(const void *payload,int len)
{
  byte gotByte;
      
  //Write the payload
  radio.stopListening();  
  
  if(radio.write(payload,len))
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
          radio.read(&gotByte,1);                  // Read it, and display the response time
          printf("Got response %d\n\r",gotByte);
      }
    }
  }
  else
  {
    printf("Sending failed\n");
  }
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
     
     //Check if our interrupt pin is triped before looping again if enabled
     if(TRIGGERTYPE != 0)
     {
       boolean pinState = digitalRead(INTERRUPTPIN);
       if(pinState != lastPinState)
       {
         lastPinState=pinState;
         if(pinState == LOW)
         {
             enableInterruptFlag=false;  //turn off interrupt until state change so we don't keep repeating endlessly
             interruptType=1;
             break; //let's get out of here
         }
         else
         {
             enableInterruptFlag=true;  //reset interrupt for next time
             if(TRIGGERTYPE == 1) //both on trigger and release
             {
                 interruptType=2;
                 break; //falls out and returns
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



