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

//poke the bms
uint8_t char_value[6] = {0xee, 0xc1, 0x00, 0x00, 0x00, 0xce};

//received value
uint8_t char_result[64];

void setup() {
  Serial.begin(115200);
  delay(3000);

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

  if (peripheral) {
    // discovered a peripheral, print out address, local name, and advertised service
    Serial.print("Found ");
    Serial.print(peripheral.address());
    Serial.print(" '");
    Serial.print(peripheral.localName());
    Serial.print("' ");
    Serial.print(peripheral.advertisedServiceUuid());
    Serial.println();

    // Check if the peripheral is a our SOK batter
    if (peripheral.localName() == "SOK-AA12487") {
      // stop scanning
      BLE.stopScan();

      monitorBMS(peripheral);

      // peripheral disconnected, start scanning again
      BLE.scan();
    }
  }
}

void monitorBMS(BLEDevice peripheral) {
  // connect to the peripheral
  Serial.println("Connecting ...");
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
    }
  }

  Serial.println("bms disconnected!");
}
