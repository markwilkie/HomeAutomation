
#include <SPI.h>
#include <LoRa.h>
#include "LowPower.h"

#define ID 11   //unique ID for this particular sensor
#define VBATPIN A9
#define SLEEP_CYCLES 450   //This is 1 hour, because we can sleep for up to 8 seconds at a time.
#define DRY 790
#define WET 387


// TODO
//
// - Add voltage and send it
// - Why won't this compile on VS Code??
// - Add some way to not sleep again, and then flash led when ready  (bring a pin high or something)
// - Add LED flashes to indicate what's happening

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

void setup() 
{
  pinMode(VBATPIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  blink(3,500);
    
  delay(20000);  //put here so I don't brick another board

  LoRa.setPins(8, 4, 7);  // slave select, reset, interrupt
  while(!LoRa.begin(915E6)) 
  {
    blink(1,100);
  }
}

void loop() 
{
  //Rad battery  
  float measuredvbat = analogRead(VBATPIN);
  measuredvbat *= 2;    // we divided by 2, so multiply back
  measuredvbat *= 3.3;  // Multiply by 3.3V, our reference voltage
  measuredvbat /= 1024; // convert to voltage
  int voltage=(int)measuredvbat*10.0;      // send as int where we'll convert it back
  
  //Read sensor
  blink(3,100);
  int moisture=analogRead(0); //connect sensor to Analog 0
  int perc=map(moisture, WET, DRY, 0, 100);
  perc=100-perc;

  // send packet
  LoRa.beginPacket();
  LoRa.write(1);    //SOH
  LoRa.write(ID); //unique id
  LoRa.write(perc); //percentage moisture
  LoRa.write(voltage);   //voltage
  LoRa.write(ID|perc|voltage);   //poor man's crc
  LoRa.write(4);    //EOT
  LoRa.endPacket();

  //delay(60L*1000L);

  //Go to sleep
  for(int i=0;i<SLEEP_CYCLES;i++)
  {
    blink(1,10);
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF); 
  }
}
