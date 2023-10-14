#ifndef DEVICE_WRAPPER_H
#define DEVICE_WRAPPER_H

#include "ArduinoBLE.h"

#define DEFAULT_DATA_BUFFER_LENGTH		100
#define MAX_REGISTER_VALUES		50

class BTDevice;

struct BLE_SEMAPHORE
{
	BTDevice *btDevice;   			//Pointer to our device context ball
	uint32_t startTime;				//When we sent the request
	uint16_t expectedBytes;			//The two bytes in response we're waiting for
    boolean waitingForConnection;	//True when blocking for a connection callback
	boolean waitingForResponse;		//True when blocking for a response callback to a command
};

struct REGISTER_DESCRIPTION {
	uint16_t address;
	uint8_t bytesUsed;
	const char * name;
	uint8_t type;
	float multiplier;
};

struct REGISTER_VALUE {
	uint16_t registerAddress;
	uint16_t value;
	uint32_t lastUpdateMillis = 0;
};

class BTDevice
{
	public:

		//Virtual member functions
		virtual void scanCallback(BLEDevice *myDevice,BLE_SEMAPHORE* bleSemaphore) = 0;
		virtual boolean connectCallback(BLEDevice *myDevice,BLE_SEMAPHORE* bleSemaphore) = 0;
		virtual void notifyCallback(BLEDevice *myDevice, BLECharacteristic *characteristic,BLE_SEMAPHORE* bleSemaphore) = 0;
		virtual void disconnectCallback(BLEDevice *myDevice) = 0;
		
		//Non Virtual member functions
		boolean getIsNewDataAvailable();
		const char *getPerifpheryName();
		uint8_t *getPeripheryAddress();
		BLEDevice *getBLEDevice();
		boolean isConnected();

	protected:

		void updateSemaphore(BLE_SEMAPHORE*,uint16_t expectedBytes);  //for command/responses
		void updateSemaphore(BLE_SEMAPHORE*);  //for connections

		//Context
		BLEDevice *bleDevice;    //ArduinoBLE connected device
		uint8_t peripheryAddress[6];
		boolean connected = false;
		boolean newDataAvailable;
		int lastCmdSent;

		uint8_t dataReceived[DEFAULT_DATA_BUFFER_LENGTH];
		int dataReceivedLength = 0;
		boolean dataError = false;

		REGISTER_VALUE registerValues[MAX_REGISTER_VALUES];
		int registerDescriptionSize = 0;
		int registerValueSize = 0;
		int registerExpected;
		REGISTER_VALUE invalidRegister;		

		BLECharacteristic txDeviceCharateristic;
		BLECharacteristic rxDeviceCharateristic;

		// SOK Tx and Rx service
		const char* peripheryName;	
		const char* txServiceUUID;
		const char* txCharacteristicUUID;	
		const char* rxServiceUUID;	
		const char* rxCharacteristicUUID;
};

#endif