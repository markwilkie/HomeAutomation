
#include <SPI.h>
#include <LoRa.h>
#include <avr/eeprom.h>
#include "LowPower.h"
#include "debug.h"

#define BASE_ID 15
#define BIT0 6
#define BIT1 3
#define BIT2 2
#define BIT3 10
#define VBATPIN A9

#define SLEEP_CYCLES 450   //This is 1 hour, because we can sleep for up to 8 seconds at a time.
#define REBOOT_CYCLES 12   //How many sleep cycle before reboot  (if sleeps for 1 hour, then 12 will reboot every 12 hours)

//Default values
#define DRY 790
#define WET 387

struct ConfigStruct
{
  int dryValue;
  int wetValue;
} config;

int boardId;
int rebootNum = 0;

// create a standard reset function
void(* resetFunc) (void) = 0; 

void setup() 
{
  #if defined(DEBUG)
  Serial.begin(115200);
  #endif
  
  pinMode(VBATPIN, INPUT);        //used to read battery level 
  pinMode(LED_BUILTIN, OUTPUT);   //pin 13 on ATMEL32A4
  blink(3,500);

  //pull up for binary board id
  pinMode(BIT0,INPUT_PULLUP);
  pinMode(BIT1,INPUT_PULLUP);
  pinMode(BIT2,INPUT_PULLUP);
  pinMode(BIT3,INPUT_PULLUP);  

  //Read from eeprom
  eeprom_read_block((void*)&config, (void*)0, sizeof(config));

  //Set defaults if not yet calibrated
  if(config.wetValue<0)
    config.wetValue=WET;
  if(config.dryValue<0)
    config.dryValue=WET;
    
  #if defined(DEBUG)
  Serial.println("Stored Config Values");
  Serial.print(config.dryValue);
  Serial.print(" ");
  Serial.println(config.wetValue);
  #endif

  //put here so I upload again w/o reloading bootloader
  #if !defined(DEBUG)
  delay(20000);  

  //read board id from 4 pins which represent a binary number
  boardId = getBoardID();

  //start radio, and loop forever if it won't connect
  LoRa.setPins(8, 4, 7);  // slave select, reset, interrupt
  while(!LoRa.begin(915E6)) 
  {
    blink(1,100);
  }
  #endif
}

void handleIncomingSerial()
{
  char incomingByte = Serial.read();
  if(incomingByte=='d' || incomingByte=='D')
    config.dryValue=analogRead(0);

  if(incomingByte=='w' || incomingByte=='W')
    config.wetValue=analogRead(0);

  eeprom_write_block((const void*)&config, (void*)0, sizeof(config));

  #if defined(DEBUG)
  Serial.println("Saved New Config Values");
  Serial.print(config.dryValue);
  Serial.print(" ");
  Serial.println(config.wetValue);
  #endif    
}

void loop() 
{
  //Read battery  
  float measuredvbat = analogRead(VBATPIN);
  measuredvbat *= 1.3;    // board based voltage divider
  measuredvbat *= 3.3;  // Multiply by 3.3V, our reference voltage
  measuredvbat /= 1024; // convert to voltage
  int voltage=(int)(measuredvbat*10.0);      // send as int where we'll convert it back
  
  //Read sensor
  blink(3,100);
  int moisture=analogRead(0); //connect sensor to Analog 0
  int perc=map(moisture, config.wetValue, config.dryValue, 0, 100);
  perc=100-perc;

  #if defined(DEBUG)
  Serial.print("Moisture: (raw/perc) ");
  Serial.print(moisture);
  Serial.print(" ");
  Serial.println(perc);

  Serial.println("Stored Config Values");
  Serial.print(config.dryValue);
  Serial.print(" ");
  Serial.println(config.wetValue);

  //wait 5 seconds
  for(int i=0;i<10;i++)
  {
    if (Serial.available() > 0)
      handleIncomingSerial();
    delay(500);
  }
  #endif


  #if !defined(DEBUG)
  // send packet
  LoRa.beginPacket();
  LoRa.write(1);    //SOH
  LoRa.write(boardId); //unique id based on which pins are brought low
  LoRa.write(perc); //percentage moisture as a whole number
  LoRa.write(voltage);   //voltage * 10 to imply decimal point
  LoRa.write(boardId|perc|voltage);   //poor man's crc
  LoRa.write(4);    //EOT
  LoRa.endPacket();

  //delay(60L*1000L);

  //Go to sleep
  for(int i=0;i<SLEEP_CYCLES;i++)
  {
    blink(1,10);
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF); 
  }

  //Time to reboot?
  if(rebootNum>REBOOT_CYCLES)
  {
    resetFunc();  // reset the Arduino via software function
  }
  #endif
}

int getBoardID()
{
  int id=0;
  if(!digitalRead(BIT0))
    id+=1;
  if(!digitalRead(BIT1))
    id+=2;
  if(!digitalRead(BIT2))
    id+=4;
  if(!digitalRead(BIT3))
    id+=8;

  id+=BASE_ID;
  return id;
}

void blink(int blinks,int duration) 
{
  for(int i=0;i<blinks;i++)
  {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(duration);
    digitalWrite(LED_BUILTIN, LOW);
    delay(duration);
  }
}
