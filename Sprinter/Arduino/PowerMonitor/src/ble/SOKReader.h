#ifndef SOK_READER_H
#define SOK_READER_H

#include "BTDevice.hpp"
#include "ArduinoBLE.h"
#include "../logging/logger.h"

extern Logger logger;

#define SOK_BLE_STALE 20000
#define PROTECTION_COUNT 50

class SOKReader : public BTDevice
{

public:

	SOKReader(const char* _peripheryName, int batteryNumber);

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
	int getCycles();
	float getTemperature();
	boolean isDMOS();
	boolean isCMOS();
	boolean isProtected();
	boolean isHeating();

	boolean isCurrent();

private:

	int bytesToInt(uint8_t *bytes, int len, boolean isSigned) ;	

	int sendCommandCounter=0;
	int batteryNumber=-1;

	//variables
	long lastHeardTime;
	int soc;
	float volts;
	float amps;
	float capacity;
	int cycles;
	float temperature;
	boolean dmosFlag;
	boolean cmosFlag;
	boolean protectedFlag;
	boolean heatingFlag;
};

#endif