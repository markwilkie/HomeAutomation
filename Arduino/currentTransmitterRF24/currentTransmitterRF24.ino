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
#define CPIN0   0     // adc pin for 100A ct sensor
#define CPIN1   1     // adc pin for 100A ct sensor
#define CPIN2   2     // adc pin for 30A ct sensor
#define CPIN3   3     // adc pin for 100A ct sensor
#define CPIN4   4     // adc pin for 100A ct sensor
#define CPIN5   5     // adc pin for 100A ct sensor

//Protocol constants
const byte ADDR0 = B00000000;    //Address byte for amps message
const byte ADDR1 = B00000001;    //Address byte for amps message
const byte ADDR2 = B00000010;    //Address byte for amps message
const byte ADDR3 = B00000011;    //Address byte for amps message
const byte ADDR4 = B00000100;    //Address byte for amps message
const byte ADDR5 = B00000101;    //Address byte for amps message

//Constants for calculations
const int ICAL_30A = 30;             //see comment above for explanation  (30 for 30A CT, and 58 for 100A CT)
const int ICAL_100A = 58;
const double ADCRES = 1024.0;        //10bit adc
const double VOLTS = 120.4;          //approx household voltage
const int CROSSINGS = 20;            // how many times we sample (20 is 10 full waveforms)
const int TIMEOUT = 5000;            //timeout for sampling
const int READ_INTERVAL = 2000;      //Interval between sensor reads
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
  radio.openWritingPipe(0xF0F0F0F0E1LL);        // Write to "Power" pipe
  //radio.openReadingPipe(1,0xF0F0F0F0D2LL);      // Open a reading pipe on address 0, pipe 1  -- no need to read, ACK is fine
  radio.startListening();                 // Start listening
  radio.powerUp();
  radio.printDetails();                   // Dump the configuration of the rf unit for debugging
}
 
//Get ADC readings and send them out via transmitter 
void loop()
{
   //Read adc, calculate amps, then send for each sensor
   readSendAmps(CPIN0,ICAL_100A,ADDR0);
   delay(READ_INTERVAL);
   readSendAmps(CPIN1,ICAL_100A,ADDR1);
   delay(READ_INTERVAL);
   readSendAmps(CPIN2,ICAL_100A,ADDR2);
   delay(READ_INTERVAL);
   readSendAmps(CPIN3,ICAL_100A,ADDR3);
   delay(READ_INTERVAL);
   readSendAmps(CPIN4,ICAL_100A,ADDR4);
   delay(READ_INTERVAL);
   readSendAmps(CPIN5,ICAL_100A,ADDR5);
   delay(READ_INTERVAL);   
    
   //Wait before looping
   delay(TRANS_INTERVAL);
}

//Read and send w/ blinky LED
void readSendAmps(int pin,int iCal,byte addr)
{
  //turn on LED
  digitalWrite(LED_PIN, HIGH);

  //Read, then send amps
  double amps = readAmps(pin,iCal);  //pin to read, calibration value
  //Serial.println(amps);
  sendAmps(amps,addr);              //amps to transmit
  
  //turn off LED
  digitalWrite(LED_PIN, LOW);
}

//Read Amps
double readAmps(int samplePin,int iCal)
{
   //Get adcRMSVal
   double adcRMSVal=getAdcRMSVal(samplePin);

   //Let calc the values now
   double aRef = readVcc();
   double ratio = iCal *(aRef / ADCRES);
   double amps = ratio*adcRMSVal; 
   
   //double sensv = (adcRMSVal/ADCRES)*aRef;
   //double kW = ((amps*VOLTS)*2.0)/1000.0;  // doubled due to 220v
   
   return amps;
}

//Send Amps
void sendAmps(double amps,byte addr)
{
  Serial.println(amps);
  printf("Sending addr: %d  amps: %f\n",addr,amps);
    
  //Fill data buffer
  data[0]=addr;
  float famps=(float)amps;
  memcpy(&data[1],&famps,4);
    
  //printf("Sending bytes: ");
  //for(int i=0;i<5;i++) {
  //  printf("%c ",data[i]); }
  //printf("\n"); 

  //Send it
  radioSend(data,5);
  
   //Send amps and addr
   //radioSend(&addr,1);
   //radioSend(&amps,4);
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
 
//Read the ADC and return the voltage 
double getAdcRMSVal(int samplePin)
{
    double adcRMSVal=0;
    
    int crossCount = 0;                             //Used to measure number of times threshold is crossed.
    int numberOfSamples = 0;                        //This is now incremented  
    double sumI = 0; //reset sum
    long startSample;
    int lastADCSample;
    boolean lastVCross, checkVCross;
    double lastFiltered, filtered = 0;

    //-------------------------------------------------------------------------------------------------------------------------
    // 1) Waits for the waveform to be close to 'zero' (500 adc) part in sin curve.
    //-------------------------------------------------------------------------------------------------------------------------
    boolean st=false;                                  //an indicator to exit the while loop
    unsigned long start = millis();    //millis()-start makes sure it doesnt get stuck in the loop if there is an error.
 
    while(st==false)                                   //the while loop...
    {
       startSample = analogRead(samplePin);                    //using the voltage waveform
       if ((startSample < 550) && (startSample > 440)) st=true;  //check its within range
       if ((millis()-start)>TIMEOUT) st = true;
    }
    int ADCSample = startSample; //seed
      
    //-------------------------------------------------------------------------------------------------------------------------
    // 2) Main measurment loop
    //------------------------------------------------------------------------------------------------------------------------- 
    start = millis(); 
    while ((crossCount < CROSSINGS) && ((millis()-start)<TIMEOUT)) 
     {
      numberOfSamples++;                            //Count number of times looped.

      lastADCSample = ADCSample;
      ADCSample = analogRead(samplePin); //read pin
      
      lastFiltered = filtered;
      filtered = 0.996*(lastFiltered+ADCSample-lastADCSample);  //get rid of the bias

      //Add up sums of squares because we're going to sqrt the average at the end
      sumI += (filtered * filtered);
      
      //-----------------------------------------------------------------------------
      // G) Find the number of times the voltage has crossed the initial voltage
      //    - every 2 crosses we will have sampled 1 wavelength 
      //    - so this method allows us to sample an integer number of half wavelengths which increases accuracy
      //-----------------------------------------------------------------------------       
      lastVCross = checkVCross;                     
      if (ADCSample > startSample) checkVCross = true; 
                       else checkVCross = false;
      if (numberOfSamples==1) lastVCross = checkVCross;                  
                       
      if (lastVCross != checkVCross) crossCount++;
    }
    
    //Root-mean-square ADC readings
    adcRMSVal=sqrt(sumI / numberOfSamples);
    
    return adcRMSVal;
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


