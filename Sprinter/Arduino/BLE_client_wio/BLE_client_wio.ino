/**
 * A BLE client example that is rich in capabilities.
 * There is a lot new capabilities implemented.
 * author unknown
 * updated by chegewara
 */

#include "rpcBLEDevice.h"
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// The remote service we wish to connect to.
//static BLEUUID serviceUUID(0xFFE0);
// The characteristic of the remote service we are interested in.
//static BLEUUID    charUUID(0xFFE1);

// The remote service we wish to connect to.
static BLEUUID serviceUUID("0000ffe0-0000-1000-8000-00805f9b34fb");
// The characteristic of the remote service we are interested in being notified about
static BLEUUID    charUUID("0000ffe1-0000-1000-8000-00805f9b34fb");
// The characteristic of the remote service to send commands to
static BLEUUID    commandUUID("0000ffe2-0000-1000-8000-00805f9b34fb");

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLERemoteCharacteristic* pCommandCharacteristic;
static BLEAdvertisedDevice* myDevice;
uint8_t bd_addr[6] = {0x21, 0x10, 0x04, 0x08, 0x22, 0x20};
BLEAddress BattServer(bd_addr);

//poke the bms
uint8_t char_value[6] = {0xee, 0xc1, 0x00, 0x00, 0x00, 0xce};

static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    Serial.print("Notify callback for characteristic ");
    Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
    Serial.print(" of data length ");
    Serial.println(length);
    Serial.print("data: ");
    for(int i=0;i<length;i++)
      Serial.print(pData[i],HEX);

    Serial.println("");

      
    //Serial.print(*(uint8_t *)pData);
}


class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("onDisconnect");
  }
};


bool connectToServer() {
    Serial.print("Forming a connection to ");
    Serial.println(myDevice->getAddress().toString().c_str());
    
    BLEClient*  pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->setClientCallbacks(new MyClientCallback());
	

    // Connect to the remove BLE Server.
    pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    Serial.println(" - Connected to server");

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    Serial.println(serviceUUID.toString().c_str());
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our service");


    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find our notify characteristic UUID: ");
      Serial.println(charUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our notify characteristic");


    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pCommandCharacteristic = pRemoteService->getCharacteristic(commandUUID);
    if (pCommandCharacteristic == nullptr) {
      Serial.print("Failed to find our command characteristic UUID: ");
      Serial.println(charUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.print(" - Found our command characteristic  ");
    Serial.println(pCommandCharacteristic->toString().c_str());

    // Read the value of the characteristic.
    //if(pRemoteCharacteristic->canRead()) {
    //  Serial.println(" -  can  read  start");
    //  std::string value = pRemoteCharacteristic->readValue();
    //  Serial.print("The characteristic value was: ");
    //  Serial.println(value.c_str());
    //}

    Serial.println("before notify registration");
    
    if(pRemoteCharacteristic->canNotify())
    {
      Serial.println("registering");
      //pRemoteCharacteristic->registerForNotify(notifyCallback);
      const uint8_t notifyOn[] = {0x1, 0x0};
      Serial.println(pRemoteCharacteristic->getDescriptor(charUUID)->toString().c_str());
      pRemoteCharacteristic->getDescriptor(charUUID)->writeValue((uint8_t*)notifyOn, 2, true);
      pRemoteCharacteristic->registerForNotify(notifyCallback);
    }

    Serial.println("after notify registration");

    connected = true;
    return true;
}
/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());
	  
    // We have found a device, let us now see if it contains the service we are looking for.
    if (memcmp(advertisedDevice.getAddress().getNative(),BattServer.getNative(), 6) == 0) {
      Serial.print("BATT Device found: ");
      Serial.println(advertisedDevice.toString().c_str());
      BLEDevice::getScan()->stop();
      Serial.println("new BLEAdvertisedDevice");
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      Serial.println("new BLEAdvertisedDevice done");
      doConnect = true;
      doScan = true;	
  } // onResult
  }
}; // MyAdvertisedDeviceCallbacks


void setup() {
  Serial.begin(115200);
  while(!Serial){};
  delay(2000);
  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
} // End of setup.


// This is the Arduino main loop function.
void loop() {
	
  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are 
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
    } else {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    }
    doConnect = false;
  }

  if(connected)
  {
    Serial.print(".");
 
    //if(pCommandCharacteristic->canWrite())
    //{
       //Serial.print("*");
      pCommandCharacteristic->writeValue(char_value,6);
    //}
  }
  
  delay(1000);
} // End of loop
