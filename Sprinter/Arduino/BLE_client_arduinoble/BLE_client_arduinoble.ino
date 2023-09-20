/*
  SensorTag Button

  This example scans for Bluetooth® Low Energy peripherals until a TI SensorTag is discovered.
  It then connects to it, discovers the attributes of the 0xffe0 service,
  subscribes to the Simple Key Characteristic (UUID 0xffe1). When a button is
  pressed on the SensorTag a notification is received and the button state is
  outputted to the Serial Monitor when one is pressed.

  The circuit:
  - Arduino MKR WiFi 1010, Arduino Uno WiFi Rev2 board, Arduino Nano 33 IoT,
    Arduino Nano 33 BLE, or Arduino Nano 33 BLE Sense board.
  - TI SensorTag

  This example code is in the public domain.
*/

#include <ArduinoBLE.h>

#define LGFX_USE_V1         // set to use new version of library
#include <LovyanGFX.hpp>    // main library
#include "LGFX_ST32-SC01Plus.hpp"

static LGFX lcd;            // declare display variable

//BLE devices
BLEDevice bms;
BLEDevice charger;

//poke the bms
uint8_t char_value[6] = {0xee, 0xc1, 0x00, 0x00, 0x00, 0xce};

//received value
char char_result[64];

void setup() {
  Serial.begin(115200);
  delay(3000);

  //init screen
  lcd.init();

  // Setting display to landscape
  if (lcd.width() < lcd.height()) lcd.setRotation(lcd.getRotation() ^ 1);

  lcd.setCursor(0,0);
  lcd.printf("Ready to start scanning");  

  // begin initialization
  if (!BLE.begin()) {
    Serial.println("starting Bluetooth® Low Energy module failed!");

    while (1);
  }

  Serial.println("Bluetooth® Low Energy Central - SOK bms");

  // start scanning for peripherals
  BLE.scan();
}

void loop() {
  // check if a peripheral has been discovered
  BLEDevice peripheral = BLE.available();

  if (peripheral) 
  {
    // discovered a peripheral, print out address, local name, and advertised service
    Serial.print("Found ");
    Serial.print(peripheral.address());
    Serial.print(" '");
    Serial.print(peripheral.localName());
    Serial.print("' ");
    Serial.print(peripheral.advertisedServiceUuid());
    Serial.println();

    // Check if the peripheral is a our SOK batter
    if (peripheral.localName() == "SOK-AA12487" && charger) 
    {
      bms=peripheral;
      monitorBMS(bms);

      // peripheral disconnected, start scanning again
      BLE.scan();
    }

    if (peripheral.localName() == "BT-TH-66F94E1C    " && !charger)   //yes, spaces are intentional
    {
      charger=peripheral;
      monitorCharger(charger);

      // peripheral disconnected, start scanning again
      BLE.scan();
    }    

    //If we're all connected, we can stop scanning
    if(bms && charger)
    {
      Serial.println("Both connected!!  (stopping scan)");
      BLE.stopScan();
    }
  }
}

void monitorCharger(BLEDevice peripheral) 
{
  // connect to the peripheral
  Serial.println("Connecting to charger...");
  if (peripheral.connect()) {
    Serial.println("Connected");
  } else {
    Serial.println("Failed to connect!");
    return;
  }

  // discover peripheral attributes
  Serial.println("Discovering service 0xffd0 ...");
  if (peripheral.discoverService("ffd0")) 
  {
    Serial.println("Main Service discovered");
  } 
  else 
  {
    Serial.println("Attribute discovery failed.");
    peripheral.disconnect();

    while (1);
    return;
  }  
}

void monitorBMS(BLEDevice peripheral) 
{
  // connect to the peripheral
  Serial.println("Connecting to bms...");
  if (peripheral.connect()) {
    Serial.println("Connected");
  } else {
    Serial.println("Failed to connect!");
    return;
  }

  // discover peripheral attributes
  Serial.println("Discovering service 0xffe0 ...");
  if (peripheral.discoverService("ffe0")) {
    Serial.println("Main Service discovered");
  } else {
    Serial.println("Attribute discovery failed.");
    peripheral.disconnect();

    while (1);
    return;
  }

  // retrieve the bms characteristic
  BLECharacteristic bmsCharacteristic = peripheral.characteristic("ffe1");

  // subscribe to the characteristic
  Serial.println("Subscribing to bms key characteristic ...");
  if (!bmsCharacteristic) {
    Serial.println("no bms characteristic found!");
    peripheral.disconnect();
    return;
  } else if (!bmsCharacteristic.canSubscribe()) {
    Serial.println("bms characteristic is not subscribable!");
    peripheral.disconnect();
    return;
  } else if (!bmsCharacteristic.subscribe()) {
    Serial.println("subscription failed!");
    peripheral.disconnect();
    return;
  } else {
    Serial.println("Subscribed");
  }

  // retrieve the bms write characteristic
  BLECharacteristic bmsCmdCharacteristic = peripheral.characteristic("ffe2");
  if (!bmsCmdCharacteristic) {
    Serial.println("no bms cmd characteristic found!");
    peripheral.disconnect();
    return;
  } else if (!bmsCmdCharacteristic.canWrite()) {
    Serial.println("bms cmd characteristic is not writable!");
    peripheral.disconnect();
    return;
  } else {
    Serial.println("Got bms cmd characteristic");
  }
 

  long startTime = millis();
  while (peripheral.connected()) {
    // while the peripheral is connected, poke every 1 second
    if(millis()>=(startTime+1000))
    {
      bmsCmdCharacteristic.writeValue(char_value,6);
      startTime=millis();
    }

    // check if the value of the characteristic has been updated
    if (bmsCharacteristic.valueUpdated()) {
      int len=bmsCharacteristic.valueLength();
      Serial.print("Recv bytes: "); Serial.println(len);
      bmsCharacteristic.readValue(char_result,len);
      for(int i=0;i<len;i++)
      {
        Serial.print(char_result[i],HEX);
        Serial.print(" ");
      }
      Serial.println("");

      //Parse
      if(char_result[0]==0xCC && char_result[1]==0xF0)
      {
        int soc=bytesToInt(char_result+16,1,false);
        float volts=bytesToInt(char_result+2,3,false)*.001;
        float amps=bytesToInt(char_result+5,3,true)*.001;
        Serial.print("SOC: ");   Serial.println(soc);
        lcd.setCursor(0,20);
        lcd.printf("SOC: %d", soc);
        Serial.print("Volts: ");   Serial.println(volts);
        lcd.setCursor(0,40);
        lcd.printf("Volts: %f", volts);        
        Serial.print("Amps: ");   Serial.println(amps);
        lcd.setCursor(0,60);
        lcd.printf("Amps: %f", amps);             
      }
      Serial.println("----------------");
    }
  }

  Serial.println("bms disconnected!");
}

//Boolean is to force signed 2 compliment if true
int bytesToInt(char*bytes, int len, boolean isSigned) 
{
    int parseInt=0;
    for(int i=0;i<len;i++)
    {
      parseInt=parseInt|bytes[i]<<(i*8);
        //int myInt1 = bytes[0] + (bytes[1] << 8) + (bytes[2] << 16) + (bytes[3] << 24);      
    }

    //Parse and shift if signed
    if(isSigned)
    {
        int shiftAmount=(len*8)-1;
        int shift1=(parseInt >> shiftAmount) << 31;
        int shift2=(parseInt << 32-shiftAmount) >> (32-shiftAmount);
        parseInt=shift1|shift2;
    }
    
    return parseInt;
}

