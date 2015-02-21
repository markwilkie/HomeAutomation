#include "Arduino.h"
#include "pins_arduino.h" 
#include "limits.h"
#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"
#include <avr/sleep.h>
#include <avr/power.h>

//sleep decrarations
typedef enum { wdt_16ms = 0, wdt_32ms, wdt_64ms, wdt_128ms, wdt_250ms, wdt_500ms, wdt_1s, wdt_2s, wdt_4s, wdt_8s } wdt_prescalar_e;

// Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins 9 & 10 
RF24 radio(9,10);
byte data[5];  //data buffer to send
int sleep_cycles_remaining; //var tracking how many sleep cycles we've been through
 
//Define pins
#define LED_PIN 4     // LED pin (turns on when reading/transmitting)
// which analog pin to connect
#define THERMISTORPIN A1
// resistance at 25 degrees C
#define THERMISTORNOMINAL 10000      
// temp. for nominal resistance (almost always 25 C)
#define TEMPERATURENOMINAL 25   
// how many samples to take and average, more takes longer but is more 'smooth'
#define NUMSAMPLES 10
// The beta coefficient of the thermistor (usually 3000-4000)
#define BCOEFFICIENT 3610 //For part number NTCS0603E3103HMT
// the value of the 'other' resistor
#define SERIESRESISTOR 10000    
//unit number when there are multiple temp sensors
//
// 0-Outside
// 1-Inside
#define UNITNUM 1   //This one will need to change for each physical Arduino
//Sleep cycles wanted  (75 is 10 min assuming 8s timer)
#define SLEEPCYCLES 75
 
int samples[NUMSAMPLES];

//Set things up
void setup()
{
   Serial.begin(57600);
   printf_begin();
   pinMode(LED_PIN, OUTPUT);     
   
    radio.begin();
    radio.setAutoAck(1);                    // Ensure autoACK is enabled
    radio.enableAckPayload();               // Allow optional ack payloads
    radio.setRetries(0,5);                  // Smallest time between retries, max no. of retries
    radio.setPayloadSize(5);                // Here we are sending 5-byte payloads 
    radio.openWritingPipe(0xF0F0F0F0E3LL);  // Write to "Temperature" pipe
    radio.startListening();                 // Start listening
    radio.powerUp();
    radio.printDetails();                   // Dump the configuration of the rf unit for debugging
  
    //Setup sleep and watchdog
    setup_watchdog(wdt_8s); //wdt_8s
}
 
//Get ADC readings and send them out via transmitter 
void loop()
{
   //Read adc, calculate amps, then send for each sensor
   readSendTemp(THERMISTORPIN);

   //power down radio as we get ready to sleep  
   radio.powerDown();
   
   //Wait for a minute (not sure why)
   delay(50);

   // Sleep the MCU.  The watchdog timer will awaken in a short while, and
   // continue execution here.
   sleep_cycles_remaining=SLEEPCYCLES;
   while(sleep_cycles_remaining)
   {
     do_sleep();   
     delay(50);  //again, this seems to be needed
   }
     
   //Now that we're awake, turn the radio back on
   radio.powerUp();
}

//Read and send w/ blinky LED
void readSendTemp(int pin)
{
  //turn on LED
  digitalWrite(LED_PIN, HIGH);

  //Read, then send temeperature
  float temp = readTemp(pin);  //pin to read
  //Serial.println(temp);
  sendTemp(temp);              //temp to transmit
 
  //turn off LED
  digitalWrite(LED_PIN, LOW);
}

//Read Amps
float readTemp(int samplePin)
{
  uint8_t i;
  float average;
 
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

//Send Temperature
void sendTemp(float temp)
{
  //Serial.println(temp);
  //printf("Sending temp: %f\n",temp);
    
  //Fill data buffer
  data[0]=(byte)UNITNUM;
  memcpy(&data[1],&temp,4);
    
  //Send it
  radioSend(data,5);
}

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

void do_sleep(void)
{
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); // sleep mode is set here
  sleep_enable();
  sleep_mode();                        // System sleeps here
  sleep_disable();                     // System continues execution here when watchdog timed out
}



