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

class BTDeviceWrapper
{
	public:

		BLEDevice *bleDevice;    //ArduinoBLE connected device
		const char *peripheryName;
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

		const char* txServiceUUID;
		const char* txCharacteristicUUID;
		const char* rxServiceUUID;
		const char* rxCharacteristicUUID;		

		BLECharacteristic txDeviceCharateristic;
		BLECharacteristic rxDeviceCharateristic;

		void initfoo(const char *_name,const char *_txSvcUUID,const char *_txChaUUID,const char *_rxSvcUUID,const char *_rxChaUUID);
};

#endif