#ifndef DEVICE_WRAPPER_H
#define DEVICE_WRAPPER_H

#include <NimBLEDevice.h>

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
		virtual void scanCallback(NimBLEAdvertisedDevice *myDevice, BLE_SEMAPHORE* bleSemaphore) = 0;
		virtual boolean connectCallback(NimBLEClient *myClient, BLE_SEMAPHORE* bleSemaphore) = 0;
		virtual void notifyCallback(NimBLERemoteCharacteristic *characteristic, uint8_t *pData, size_t length, BLE_SEMAPHORE* bleSemaphore) = 0;
		virtual void disconnectCallback(NimBLEClient *myClient) = 0;
		virtual bool isCurrent() = 0;  // Check if device data is fresh (not stale)
		
		//Non Virtual member functions
		boolean getIsNewDataAvailable();
		const char *getPerifpheryName();
		uint8_t *getPeripheryAddress();
		NimBLEClient *getBLEClient();
		NimBLERemoteCharacteristic *getRxCharacteristic() { return rxDeviceCharateristic; }
		NimBLERemoteCharacteristic *getTxCharacteristic() { return txDeviceCharateristic; }
		void setCharacteristics(NimBLEClient* client, NimBLERemoteCharacteristic* tx, NimBLERemoteCharacteristic* rx) {
			bleClient = client;
			txDeviceCharateristic = tx;
			rxDeviceCharateristic = rx;
			connected = true;
		}
		boolean isConnected();
		void disconnect();

	protected:

		void updateSemaphore(BLE_SEMAPHORE*,uint16_t expectedBytes);  //for command/responses
		void updateSemaphore(BLE_SEMAPHORE*);  //for connections

		//Context
		NimBLEClient *bleClient = nullptr;    //NimBLE client for connected device
		NimBLEAddress bleAddress;             //Address of the device
		uint8_t peripheryAddress[6];
		boolean connected = false;
		boolean newDataAvailable;
		int lastCmdSent = -1;  // -1 means no command sent yet

		uint8_t dataReceived[DEFAULT_DATA_BUFFER_LENGTH];
		int dataReceivedLength = 0;
		boolean dataError = false;

		REGISTER_VALUE registerValues[MAX_REGISTER_VALUES];
		int registerDescriptionSize = 0;
		int registerValueSize = 0;
		int registerExpected;
		REGISTER_VALUE invalidRegister;		

		NimBLERemoteCharacteristic *txDeviceCharateristic = nullptr;
		NimBLERemoteCharacteristic *rxDeviceCharateristic = nullptr;

		// SOK Tx and Rx service
		const char* peripheryName;	
		const char* txServiceUUID;
		const char* txCharacteristicUUID;	
		const char* rxServiceUUID;	
		const char* rxCharacteristicUUID;
};

#endif