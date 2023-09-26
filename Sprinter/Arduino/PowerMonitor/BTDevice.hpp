#ifndef DEVICE_WRAPPER_H
#define DEVICE_WRAPPER_H

#include "ArduinoBLE.h"

#define DEFAULT_DATA_BUFFER_LENGTH		100
#define MAX_REGISTER_VALUES		50

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
		virtual void notifyCallback(BLEDevice *myDevice, BLECharacteristic *characteristic) = 0;
		virtual void scanCallback(BLEDevice *myDevice) = 0;
		virtual boolean connectCallback(BLEDevice *myDevice) = 0;
		virtual void disconnectCallback(BLEDevice *myDevice) = 0;
		
		//Non Virtual member functions
		boolean getIsNewDataAvailable();

		//Context
		BLEDevice *bleDevice;    //ArduinoBLE connected device
		uint8_t peripheryAddress[6];
		boolean connected = false;
		boolean newDataAvailable;
		long lastCheckedTime;
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

	protected:

		// SOK Tx and Rx service
		const char* peripheryName;	
		const char* txServiceUUID;
		const char* txCharacteristicUUID;	
		const char* rxServiceUUID;	
		const char* rxCharacteristicUUID;
};

#endif