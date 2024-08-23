#include "SOKReader.h"

extern void mainNotifyCallback(BLEDevice peripheral, BLECharacteristic characteristic);

SOKReader::SOKReader()
{
	peripheryName="SOK-AA12487";
	txServiceUUID="ffe0";
	txCharacteristicUUID="ffe2";
	rxServiceUUID="ffe0";
	rxCharacteristicUUID="ffe1";	
}

void SOKReader::scanCallback(BLEDevice *peripheral,BLE_SEMAPHORE* bleSemaphore) 
{
	if (peripheral->localName() == peripheryName)
	{
		//Set device
		bleDevice=peripheral;

		//Make sure we're not busy
		if(bleSemaphore->waitingForResponse || bleSemaphore->waitingForConnection)
		{
			logger.log("BLE busy with %s",bleSemaphore->btDevice->getPerifpheryName());
			return;
		}

		//Set semaphore for connect
		updateSemaphore(bleSemaphore);

		logger.log("Found targeted SOK device, attempting connection");
		memcpy(peripheryAddress,peripheral->address().c_str(),6);
		peripheral->connect();	
	} 
}


boolean SOKReader::connectCallback(BLEDevice *myDevice,BLE_SEMAPHORE* bleSemaphore) 
{
	logger.log("SOK: Discovering Tx service: %s",txServiceUUID);
	if(myDevice->discoverService(txServiceUUID))
	{
		logger.log("SOK Tx service %s discovered",txServiceUUID);
		txDeviceCharateristic=myDevice->characteristic(txCharacteristicUUID);
		if(!txDeviceCharateristic)
		{
			logger.log("ERROR: SOK Tx characteristic not discovered, disconnecting");
			myDevice->disconnect();
			connected=false;
			return false;
		}
	} 
	else 
	{
		logger.log("ERROR: SOK Tx service not discovered, disconnecting");
		myDevice->disconnect();
		connected=false;
		return false;		
	}

	logger.log("SOK: Discovering Rx service: %s",rxServiceUUID);
	if(myDevice->discoverService(rxServiceUUID))
	{
		logger.log(INFO,"SOK Rx service %s discovered.  Now looking for characteristic %s",rxServiceUUID,rxCharacteristicUUID);
		if(rxDeviceCharateristic=myDevice->characteristic(rxCharacteristicUUID))
		{
			if(rxDeviceCharateristic.canSubscribe() && rxDeviceCharateristic.subscribe())
			{
				rxDeviceCharateristic.setEventHandler(BLEWritten, mainNotifyCallback);
			}
			else
			{
				logger.log("ERROR: SOK Rx characteristic not discovered or cannot be subscribed to, disconnecting");
				myDevice->disconnect();
				connected=false;
				return false;
			} 
		}
	} 
	else 
	{
		logger.log("ERROR: SOK Rx service not discovered, disconnecting");
		myDevice->disconnect();
		connected=false;
		return false;
	}

	connected=true;
	logger.log("SOK found all needed services and characteristics.");

	bleSemaphore->waitingForConnection=false;
	logger.log("Releasing connect semaphore for BLE device %s",bleSemaphore->btDevice->getPerifpheryName());

	return true;
}

void SOKReader::disconnectCallback(BLEDevice *myDevice) 
{
	connected=false;
	memset(peripheryAddress, 0, 6);
}

void SOKReader::notifyCallback(BLEDevice *myDevice, BLECharacteristic *characteristic,BLE_SEMAPHORE *bleSemaphore) 
{
	int dataLen=characteristic->valueLength();
	characteristic->readValue(dataReceived,dataLen);
	dataReceivedLength = dataLen;
	newDataAvailable=true;

	//Release semaphore?
	if(bleSemaphore->expectedBytes == dataReceived[0] | (dataReceived[1]<<8))
	{
		//logger.log("Releasing response semaphore for BLE device %s",bleSemaphore->btDevice->getPerifpheryName());
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
	if(bleSemaphore->waitingForResponse)
	{
		logger.log("BLE device %s in use when another send attempt was tried",bleSemaphore->btDevice->getPerifpheryName());
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

	#ifdef SERIALLOGGER
	Serial.print("Sending command sequence to SOK: ");
	for (int i = 0; i < 6; i++) { Serial.printf("%02X ", command[i]); }
	Serial.println("");
	#endif

	txDeviceCharateristic.writeValue(command, 6);
	dataReceivedLength = 0;
	dataError = false;
	newDataAvailable = false;

	//Update semaphore
	uint16_t expectedBytes=0xCC | (0xF0 << 8);
	updateSemaphore(bleSemaphore,expectedBytes);		
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
		
		
