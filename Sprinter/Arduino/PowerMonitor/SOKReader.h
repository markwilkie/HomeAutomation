#ifndef SOK_READER_H
#define BT2_READER_H

#include "BTDeviceWrapper.hpp"
#include "ArduinoBLE.h"

class SOKReader 
{

public:

	void begin(BTDeviceWrapper *btDeviceWrapper);

	void notifyCallback(BLEDevice *myDevice, BLECharacteristic *characteristic);
	void scanCallback(BLEDevice *myDevice);
	boolean connectCallback(BLEDevice *myDevice);
	void disconnectCallback(BLEDevice *myDevice);
	
	REGISTER_VALUE *getRegister(uint16_t registerAddress);
	
	void sendReadCommand();
	boolean getIsNewDataAvailable();
	int bytesToInt(uint8_t *bytes, int len, boolean isSigned) ;

private:

	BTDeviceWrapper  *btDeviceWrapper;
};



#endif