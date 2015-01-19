/*
   Parts derived from examples by J. Coliz <maniacbug@ymail.com>
*/

#include <avr/power.h>
#include <avr/sleep.h>
#include "RF24.h"
#include "nRF24L01.h"
#include <SPI.h>
//#include <EEPROM.h>
#include "printf.h"

// Hardware configuration: Set up nRF24L01 radio on SPI bus 
RF24 radio(9,10);

byte counter = 1;    // A single byte to keep track of the data being sent back and forth
int motionPin = 2;  //only 2 and 3 are available
byte gotByte; 
const byte UNIT_NUMBER = 1;

// Sleep declarations
typedef enum { wdt_16ms = 0, wdt_32ms, wdt_64ms, wdt_128ms, wdt_250ms, wdt_500ms, wdt_1s, wdt_2s, wdt_4s, wdt_8s } wdt_prescalar_e;

  
void setup()
{
  Serial.begin(57600);
  printf_begin();
  Serial.println("Starting up...");
  
  // Setup and configure rf radio
  radio.begin();
  radio.setAutoAck(1);                    // Ensure autoACK is enabled
  radio.enableAckPayload();               // Allow optional ack payloads
  radio.setRetries(0,15);                 // Smallest time between retries, max no. of retries
  radio.setPayloadSize(1);                // Here we are sending 1-byte payloads to test the call-response speed
  radio.openWritingPipe(0xF0F0F0F0E2LL);        // Write to "Motion" pipe
  //radio.openReadingPipe(1,0xF0F0F0F0D2LL);      // Open a reading pipe on address 0, pipe 1  -- no need to read, ACK is fine
  radio.startListening();                 // Start listening
  radio.powerUp();
  radio.printDetails();                   // Dump the configuration of the rf unit for debugging
  
  //read motion
  pinMode(motionPin, INPUT_PULLUP);  //pullup is required as LOW is the trigger
  
  //send initial byte to show we're alive
  sendByte();
}

void loop(void) 
{
  printf("Delay...");
  delay(1000); //rest
  printf("OK done\n");
  sleepNow();
  sendByte();
}

void sendByte()
{
  Serial.println("Sending byte"); 
  radio.stopListening(); 
  
  // Send the counter variable to the base/master radio 
  if ( radio.write(&UNIT_NUMBER,1) )
  {                         
      if(!radio.available())
      {                             
          // If nothing in the buffer, we got an ack but it is blank
          printf("Got blank response\n\r");     
      }
      else
      {      
          while(radio.available() )
          {                      
              // If an ack with payload was received
              radio.read( &gotByte, 1 ); 
              printf("Got response %d, round-trip\n\r");
          }
      }
  
  }
  else
  {
    // If no ack response, sending failed    
    printf("Sending failed.\n\r"); 
  }

  counter++;
}

void sleepNow(void)
{
    Serial.println("Shutting things down");

    //
    //Put the radio to sleep
    radio.powerDown();

    //
    // Choose our preferred sleep mode:
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    //
    // Set sleep enable (SE) bit:
    sleep_enable();

    // Set interrupt and attach handler:
    attachInterrupt(0, pinInterrupt, LOW);
    delay(100);    
    //
    // Put the device to sleep:
    //digitalWrite(13,LOW);   // turn LED off to indicate sleep
    sleep_mode();
    //
    // Upon waking up, sketch continues from this point.
    sleep_disable();
    //digitalWrite(13,HIGH);   // turn LED on to indicate awake
    
    //Detach interrupt
    detachInterrupt(0);
    
    //Wait to make sure we're fully away (almost asuradely overkill)
    Serial.print("Waking up...");
    //delay(1000);
    //Serial.println("Done");
   
    //Turn radio back on
    radio.powerUp();
}

void pinInterrupt(void)
{
  //sleep_disable();
  //detachInterrupt(0);
}
