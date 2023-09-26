#ifndef SOK_READER_H
#define BT2_READER_H

#include "BTDevice.hpp"
#include "ArduinoBLE.h"

class SOKReader : public BTDevice
{

public:

	SOKReader();

	void notifyCallback(BLEDevice *myDevice, BLECharacteristic *characteristic);
	void scanCallback(BLEDevice *myDevice);
	boolean connectCallback(BLEDevice *myDevice);
	void disconnectCallback(BLEDevice *myDevice);

	void sendReadCommand();

	int bytesToInt(uint8_t *bytes, int len, boolean isSigned) ;	
};



#endif