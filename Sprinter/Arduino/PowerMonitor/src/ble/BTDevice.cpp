#include "BTDevice.hpp"

uint8_t *BTDevice::getPeripheryAddress()
{
	return peripheryAddress;
}

boolean BTDevice::isConnected()
{
	return connected;
}

void BTDevice::disconnect()
{
	if(bleClient && bleClient->isConnected())
		bleClient->disconnect();
}

boolean BTDevice::getIsNewDataAvailable() 
{
	boolean isNewDataAvailable = newDataAvailable;
	newDataAvailable = false;
	return (isNewDataAvailable);
}

const char *BTDevice::getPerifpheryName()
{
	return peripheryName;
}

NimBLEClient *BTDevice::getBLEClient()
{
	return bleClient;
}

void BTDevice::updateSemaphore(BLE_SEMAPHORE* bleSemaphore, uint16_t expectedBytes)
{
	//Update semaphore
	bleSemaphore->btDevice=this;
	bleSemaphore->waitingForConnection=false;
	bleSemaphore->waitingForResponse=true;
	bleSemaphore->startTime=millis();
	bleSemaphore->expectedBytes=expectedBytes;
}

void BTDevice::updateSemaphore(BLE_SEMAPHORE* bleSemaphore)
{
	//Update semaphore
	bleSemaphore->btDevice=this;
	bleSemaphore->waitingForConnection=true;
	bleSemaphore->waitingForResponse=false;
	bleSemaphore->startTime=millis();
	bleSemaphore->expectedBytes=0;
}