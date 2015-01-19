#include "Arduino.h"
#include "pins_arduino.h" 
#include "limits.h"
#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"

/*
 Get amps from ADC assuming a 1.1v reference:  =(((ADC/1023)*1.1)/62)*1800
 Get ADC from Volts:  =(Volts*1023)/1.1
 Get Volts from ADC: =(ADC/1023)*1.1
 Get Amps from ADC: =(Volts/62)*1800  (62 is burden resistor, 1800 is turns)
 Burden Resistor of CT transformer: 62 
 CT Turns 1800 
 
The ICAL value in EmonLib represents the number of amps per volt, as seen at the analogue input. ICAL = (Primary Current / secondary Current) / burden resistance
So if, for example, the spec sheet for your device says 30A/.01667A and 62 Ohm burden, then ICAL=29.02645 (or 30 practically as shown for SCT 013-030)

volt/amp calc: http://openenergymonitor.org/emon/buildingblocks/ac-power-arduino-maths
filter: http://openenergymonitor.org/emon/buildingblocks/digital-filters-for-offset-removal
code: https://github.com/openenergymonitor/EmonLib/blob/master/EmonLib.cpp
 */
 
// Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins 9 & 10 
RF24 radio(9,10);
//byte addresses[][8] = {"Node2","Master"};              // Radio pipe addresses for the 2 nodes to communicate.
byte data[5];  //data buffer to send
 
//Define pins
#define LED_PIN 8     // LED pin (turns on when reading/transmitting)
#define TEMP_PIN   0     // adc pin for 100A ct sensor

//Constants for calculations
const double ADCRES = 1024.0;        //10bit adc
const int TIMEOUT = 5000;            //timeout for sampling
const int TRANS_INTERVAL = 30000;    //Interval for sending out data via transmitter  (end of all sensor reads)
 
//Set things up
void setup()
{
   Serial.begin(57600);
   printf_begin();
   pinMode(LED_PIN, OUTPUT);     
   
  radio.begin();
  radio.setAutoAck(1);                    // Ensure autoACK is enabled
  radio.enableAckPayload();               // Allow optional ack payloads
  radio.setRetries(0,5);                 // Smallest time between retries, max no. of retries
  radio.setPayloadSize(5);                // Here we are sending 5-byte payloads 
  radio.openWritingPipe(0xF0F0F0F0E3LL);        // Write to "Power" pipe
  radio.startListening();                 // Start listening
  radio.powerUp();
  radio.printDetails();                   // Dump the configuration of the rf unit for debugging
}
 
//Get ADC readings and send them out via transmitter 
void loop()
{
   //Read adc, calculate amps, then send for each sensor
   readSendTemp(TEMP_PIN);
    
   //Wait before looping
   delay(TRANS_INTERVAL);
}

//Read and send w/ blinky LED
void readSendTemp(int pin)
{
  //turn on LED
  digitalWrite(LED_PIN, HIGH);

  //Read, then send amps
  double temp = readTemp(pin);  //pin to read
  Serial.println(temp);
  sendTemp(temp);              //temp to transmit
  
  //turn off LED
  digitalWrite(LED_PIN, LOW);
}

//Read Amps
double readTemp(int samplePin)
{
   //Get adcRMSVal
   double adcRMSVal=getAdcRMSVal(samplePin);

   //Let calc the values now
   double aRef = readVcc();
   double ratio = iCal *(aRef / ADCRES);
   double amps = ratio*adcRMSVal; 
   
   return amps;
}

//Send Amps
void sendTemp(double temp)
{
  Serial.println(temp);
  printf("Sending temp: %f\n",temp);
    
  //Fill data buffer
  data[0]=addr;
  float famps=(float)amps;
  memcpy(&data[1],&famps,4);
    
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

 
//Find out the actual voltage at the chip for good ADC calibration 
double readVcc() 
{
  long result;

  #if defined(__AVR_ATmega168__) || defined(__AVR_ATmega328__) || defined (__AVR_ATmega328P__)
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);  
  #elif defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
  ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  ADCSRB &= ~_BV(MUX5);   // Without this the function always returns -1 on the ATmega2560 http://openenergymonitor.org/emon/node/2253#comment-11432
  #elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
  ADMUX = _BV(MUX5) | _BV(MUX0);
  #elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
  ADMUX = _BV(MUX3) | _BV(MUX2);


  #endif


 #if defined(__AVR__) 
  delay(2);					// Wait for Vref to settle
  ADCSRA |= _BV(ADSC);				// Convert
  while (bit_is_set(ADCSRA,ADSC));
  result = ADCL;
  result |= ADCH<<8;
  result = 1126400L / result;		//1100mV*1024 ADC steps http://openenergymonitor.org/emon/node/1186
  return (double)(result/1000.0); 
 #elif defined(__arm__)
  return (3.3);				       //Arduino Due
 #else return (3.3);                            //Wild gess that other un-supported architectures will be running a 3.3V!
 #endif
}


