#ifndef SOK_READER_H
#define SOK_READER_H

#include "BTDevice.hpp"
#include "ArduinoBLE.h"

class SOKReader : public BTDevice
{

public:

	SOKReader();

	void scanCallback(BLEDevice *myDevice,BLE_SEMAPHORE *bleSemaphore);
	boolean connectCallback(BLEDevice *myDevice,BLE_SEMAPHORE* bleSemaphor);
	void notifyCallback(BLEDevice *myDevice, BLECharacteristic *characteristic,BLE_SEMAPHORE* bleSemaphor);
	void disconnectCallback(BLEDevice *myDevice);

	void sendReadCommand(BLE_SEMAPHORE* bleSemaphor);
	void updateValues();

	int getSoc();
	float getVolts();
	float getAmps();
	float getCapacity();

private:

	int bytesToInt(uint8_t *bytes, int len, boolean isSigned) ;	

	//variables
	int soc;
	float volts;
	float amps;
	float capacity;
};

#endif