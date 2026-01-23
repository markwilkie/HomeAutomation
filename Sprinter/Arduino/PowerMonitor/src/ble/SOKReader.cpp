#include "SOKReader.h"


SOKReader::SOKReader(const char* _peripheryName, int _batteryNumber)
{
	peripheryName=_peripheryName;
	batteryNumber=_batteryNumber;
	txServiceUUID="ffe0";
	txCharacteristicUUID="ffe2";
	rxServiceUUID="ffe0";
	rxCharacteristicUUID="ffe1";
	lastHeardTime=millis();
}

void SOKReader::scanCallback(NimBLEAdvertisedDevice *peripheral, BLE_SEMAPHORE* bleSemaphore) 
{
	if (peripheral->getName() == peripheryName)
	{
		// NO LOGGING IN SCAN CALLBACK - runs in BLE task with limited stack!
		
		//Already connected? Skip
		if(connected)
			return;

		//Already attempting connection for THIS device? Skip
		if(bleSemaphore->waitingForConnection && bleSemaphore->btDevice == this)
			return;

		//Store the address for later connection
		bleAddress = peripheral->getAddress();
		const uint8_t* addrVal = peripheral->getAddress().getVal();

		//Make sure we're not busy with a DIFFERENT device
		if((bleSemaphore->waitingForResponse || bleSemaphore->waitingForConnection) && bleSemaphore->btDevice != this)
			return;

		//Set semaphore for connect
		updateSemaphore(bleSemaphore);

		memcpy(peripheryAddress, addrVal, 6);
	} 
}


boolean SOKReader::connectCallback(NimBLEClient *myClient, BLE_SEMAPHORE* bleSemaphore) 
{
	bleClient = myClient;
	
	//logger.log(INFO,"SOK %d: Discovering Tx service: %s", batteryNumber, txServiceUUID);
	NimBLERemoteService *txService = myClient->getService(txServiceUUID);
	if(txService)
	{
		//logger.log(INFO,"SOK %d: Tx service %s discovered", batteryNumber, txServiceUUID);
		txDeviceCharateristic = txService->getCharacteristic(txCharacteristicUUID);
		if(!txDeviceCharateristic)
		{
			logger.log("ERROR: SOK %d: Tx characteristic not discovered, disconnecting", batteryNumber);
			myClient->disconnect();
			connected=false;
			return false;
		}
	} 
	else 
	{
		logger.log("ERROR: SOK %d: Tx service not discovered, disconnecting", batteryNumber);
		myClient->disconnect();
		connected=false;
		return false;		
	}

	//logger.log(INFO,"SOK %d: Discovering Rx service: %s", batteryNumber, rxServiceUUID);
	NimBLERemoteService *rxService = myClient->getService(rxServiceUUID);
	if(rxService)
	{
		//logger.log(INFO,"SOK %d: Rx service %s discovered.  Now looking for characteristic %s", batteryNumber, rxServiceUUID, rxCharacteristicUUID);
		rxDeviceCharateristic = rxService->getCharacteristic(rxCharacteristicUUID);
		if(rxDeviceCharateristic)
		{
			if(rxDeviceCharateristic->canNotify())
			{
				// Subscribe handled by main code with callback
			}
			else
			{
				logger.log("ERROR: SOK %d: Rx characteristic cannot notify, disconnecting", batteryNumber);
				myClient->disconnect();
				connected=false;
				return false;
			} 
		}
		else
		{
			logger.log("ERROR: SOK %d: Rx characteristic not discovered, disconnecting", batteryNumber);
			myClient->disconnect();
			connected=false;
			return false;
		}
	} 
	else 
	{
		logger.log("ERROR: SOK %d: Rx service not discovered, disconnecting", batteryNumber);
		myClient->disconnect();
		connected=false;
		return false;
	}

	connected=true;
	lastHeardTime=millis();
	//logger.log(INFO,"SOK %d: found all needed services and characteristics.", batteryNumber);

	bleSemaphore->waitingForConnection=false;
	//logger.log(INFO,"SOK %d: Releasing connect semaphore for BLE device %s", batteryNumber, bleSemaphore->btDevice->getPerifpheryName());

	return true;
}

void SOKReader::disconnectCallback(NimBLEClient *myClient) 
{
	logger.log(INFO, "SOK %d: disconnectCallback - Device disconnected", batteryNumber);
	logger.log(INFO, "SOK %d:   Previous state: connected=%s, bleClient=%s", 
		batteryNumber, connected ? "true" : "false", bleClient ? "valid" : "null");
	
	connected=false;
	memset(peripheryAddress, 0, 6);
	bleClient = nullptr;
	
	//logger.log(INFO, "SOK %d:   State cleared, ready for reconnection", batteryNumber);
}

void SOKReader::notifyCallback(NimBLERemoteCharacteristic *characteristic, uint8_t *pData, size_t length, BLE_SEMAPHORE *bleSemaphore) 
{
	// Minimal processing - this runs in BLE task context with limited stack
	memcpy(dataReceived, pData, length);
	dataReceivedLength = length;
	newDataAvailable=true;

	//Release semaphore?
	uint16_t receivedMarker = dataReceived[0] | (dataReceived[1]<<8);
	if(bleSemaphore->expectedBytes == receivedMarker)
	{
		bleSemaphore->waitingForResponse=false;
	}
}

//Boolean is to force signed 2 compliment if true
int SOKReader::bytesToInt(uint8_t *bytes, int len, boolean isSigned) 
{
    int parseInt=0;
    for(int i=0;i<len;i++)
    {
      parseInt=parseInt|bytes[i]<<(i*8);
        //int myInt1 = bytes[0] + (bytes[1] << 8) + (bytes[2] << 16) + (bytes[3] << 24);      
    }

    //Parse and shift if signed
    if(isSigned)
    {
        int shiftAmount=(len*8)-1;
        int shift1=(parseInt >> shiftAmount) << 31;
        int shift2=(parseInt << 32-shiftAmount) >> (32-shiftAmount);
        parseInt=shift1|shift2;
    }
    
    return parseInt;
}


void SOKReader::sendReadCommand(BLE_SEMAPHORE *bleSemaphore) 
{
	//Make sure we're clear to send
	if(bleSemaphore->waitingForResponse || bleSemaphore->waitingForConnection)
	{
		if(bleSemaphore->btDevice)
			logger.log(INFO,"SOK %d: BLE device %s in use when another send attempt was tried", batteryNumber, bleSemaphore->btDevice->getPerifpheryName());
		else
			logger.log(INFO,"SOK %d: BLE semaphore busy (no device)", batteryNumber);
		return;
	}

	//Commands I'm aware of
	//ee c1 000000 ce
	//ee c0 000000 41
	//ee c2 000000 46
	//ee c4 000000 4f

	uint8_t command[6];
	command[0] = 0xee;
	command[2] = 0x00;
	command[3] = 0x00;
	command[4] = 0x00;

	//Send xC1 or xC2 every other time
	sendCommandCounter++;
	if(sendCommandCounter%2 && sendCommandCounter<PROTECTION_COUNT)
	{
		//Gets base data plus heating and balancing
		command[1] = 0xc2;
		command[5] = 0x46;
	}
	else if(!(sendCommandCounter%2) && sendCommandCounter<PROTECTION_COUNT)
	{
		//Gets base data plus CMOS and DMOS
		command[1] = 0xc1;
		command[5] = 0xce;
	}
	else
	{
		//Get protection info
		command[1] = 0xc4;
		command[5] = 0x4f;

		sendCommandCounter=0;
	}

	// Verbose logging removed - uncomment for debugging
	// logger.log(INFO, "SOK %d: sendReadCommand - Sending command: %02X %02X %02X %02X %02X %02X",
	//	batteryNumber, command[0], command[1], command[2], command[3], command[4], command[5]);

	if(txDeviceCharateristic) {
		// Use write WITHOUT response (false) - SOK TX char likely only supports WriteNoResponse
		bool writeOk = txDeviceCharateristic->writeValue(command, 6, false);
		// Verbose logging removed - uncomment for debugging
		// logger.log(INFO, "SOK %d:   Command write %s", batteryNumber, writeOk ? "OK" : "FAILED");
	} else {
		logger.log(ERROR, "SOK %d:   ERROR - txDeviceCharateristic is null!", batteryNumber);
	}
	
	dataReceivedLength = 0;
	dataError = false;
	newDataAvailable = false;

	//Update semaphore
	uint16_t expectedBytes=0xCC | (0xF0 << 8);
	updateSemaphore(bleSemaphore,expectedBytes);
	// Verbose logging removed - uncomment for debugging
	// logger.log(INFO, "SOK %d:   Semaphore updated, expecting response marker: %04X", batteryNumber, expectedBytes);		
}

bool SOKReader::isCurrent()
{
	return (millis()-lastHeardTime)<SOK_BLE_STALE;
}

void SOKReader::updateValues()
{
	//trigger by both xC1 and xC2
	if(dataReceived[0]==0xCC && dataReceived[1]==0xF0)
    {
		lastHeardTime=millis();

		soc=bytesToInt(dataReceived+16,1,false);
		volts=bytesToInt(dataReceived+2,3,false)*.001;
		amps=bytesToInt(dataReceived+5,3,true)*.001;
		capacity=bytesToInt(dataReceived+11,3,true)*.001;
		cycles=bytesToInt(dataReceived+14,2,false);

		//debug
		//logger.log("Loop:  (cycles?): %d",cycles);
	}

	//Trigger by C1
	if(dataReceived[0]==0xCC && dataReceived[1]==0xF2)
	{	
		cmosFlag=dataReceived[2];
		dmosFlag=dataReceived[3];
		temperature=bytesToInt(dataReceived+5,2,true);
	}

	//Triggered by C2
	if(dataReceived[0]==0xCC && dataReceived[1]==0xF3)
    {
		heatingFlag=dataReceived[8];
	}	

	//Triggered by C4
	if(dataReceived[0]==0xCC && dataReceived[1]==0xF9)
    {
		//check protection state
		protectedFlag=false;
		for(int i=2;i<17;i++)
		{
			protectedFlag=protectedFlag|dataReceived[i];
		}
	}	

	newDataAvailable=false;
}

int  SOKReader::getSoc()
{
	return soc;
}

float SOKReader::getVolts()
{
	return volts;
}

float SOKReader::getAmps()
{
	return amps;
}

float SOKReader::getCapacity()
{
	return capacity;
}

int SOKReader::getCycles() 
{
	return cycles;
}

float SOKReader::getTemperature() 
{
	return temperature;
}

boolean SOKReader::isDMOS() 
{
	return dmosFlag;
}

boolean SOKReader::isCMOS() 
{
	return cmosFlag;
}

boolean SOKReader::isProtected() 
{
	return protectedFlag;
}

boolean SOKReader::isHeating() 
{
	return heatingFlag;
}
		
		
