#include "SOKReader.h"

extern void mainNotifyCallback(BLEDevice peripheral, BLECharacteristic characteristic);

void SOKReader::begin(BTDeviceWrapper *_btDevice) 
{
	btDeviceWrapper = _btDevice;
}

void SOKReader::scanCallback(BLEDevice *peripheral) 
{
	if (peripheral->localName() == btDeviceWrapper->peripheryName)
	{
		Serial.println("Found targeted SOK device, attempting connection");
		memcpy(btDeviceWrapper->peripheryAddress,peripheral->address().c_str(),6);
		peripheral->connect();
	} 
}


boolean SOKReader::connectCallback(BLEDevice *myDevice) 
{
	Serial.printf("SOK: Discovering Tx service: %s\n",btDeviceWrapper->txServiceUUID);
	if(myDevice->discoverService(btDeviceWrapper->txServiceUUID))
	{
		Serial.printf("SOK Tx service %s discovered\n",btDeviceWrapper->txServiceUUID);
		btDeviceWrapper->txDeviceCharateristic=myDevice->characteristic(btDeviceWrapper->txCharacteristicUUID);
		if(!btDeviceWrapper->txDeviceCharateristic)
		{
			Serial.printf("ERROR: SOK Tx characteristic not discovered, disconnecting\n");
			myDevice->disconnect();
			btDeviceWrapper->connected=false;
			return false;
		}
	} 
	else 
	{
		Serial.println("ERROR: SOK Tx service not discovered, disconnecting");
		myDevice->disconnect();
		btDeviceWrapper->connected=false;
		return false;		
	}

	Serial.printf("SOK: Discovering Rx service: %s\n",btDeviceWrapper->rxServiceUUID);
	Serial.println(btDeviceWrapper->txServiceUUID);
	if(myDevice->discoverService(btDeviceWrapper->rxServiceUUID))
	{
		Serial.printf("SOK Rx service %s discovered.  Now looking for characteristic %s\n",btDeviceWrapper->rxServiceUUID,btDeviceWrapper->rxCharacteristicUUID);
		if(btDeviceWrapper->rxDeviceCharateristic=myDevice->characteristic(btDeviceWrapper->rxCharacteristicUUID))
		{
			if(btDeviceWrapper->rxDeviceCharateristic.canSubscribe() && btDeviceWrapper->rxDeviceCharateristic.subscribe())
			{
				btDeviceWrapper->rxDeviceCharateristic.setEventHandler(BLEWritten, mainNotifyCallback);
			}
			else
			{
				Serial.println("ERROR: SOK Rx characteristic not discovered or cannot be subscribed to, disconnecting");
				myDevice->disconnect();
				btDeviceWrapper->connected=false;
				return false;
			} 
		}
	} 
	else 
	{
		Serial.println("ERROR: SOK Rx service not discovered, disconnecting\n");
		myDevice->disconnect();
		btDeviceWrapper->connected=false;
		return false;
	}

	btDeviceWrapper->connected=true;
	btDeviceWrapper->bleDevice=myDevice;
	Serial.println("SOK found all needed services and characteristics.");
	return true;
}

void SOKReader::disconnectCallback(BLEDevice *myDevice) 
{
	btDeviceWrapper->connected=false;
	memset(btDeviceWrapper->peripheryAddress, 0, 6);
}

void SOKReader::notifyCallback(BLEDevice *myDevice, BLECharacteristic *characteristic) 
{
	int dataLen=characteristic->valueLength();
	characteristic->readValue(btDeviceWrapper->dataReceived,dataLen);
	btDeviceWrapper->dataReceivedLength = dataLen;
	btDeviceWrapper->newDataAvailable=true;
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


void SOKReader::sendReadCommand() 
{
	uint8_t command[6];
	command[0] = 0xee;
	command[1] = 0xc1;
	command[2] = 0x00;
	command[3] = 0x00;
	command[4] = 0x00;
	command[5] = 0xce;

	Serial.print("Sending command sequence to SOK: ");
	for (int i = 0; i < 6; i++) { Serial.printf("%02X ", command[i]); }
	Serial.println("");

	btDeviceWrapper->txDeviceCharateristic.writeValue(command, 6);
	btDeviceWrapper->dataReceivedLength = 0;
	btDeviceWrapper->dataError = false;
	btDeviceWrapper->newDataAvailable = false;
}

boolean SOKReader::getIsNewDataAvailable() 
{
	boolean isNewDataAvailable = btDeviceWrapper->newDataAvailable;
	btDeviceWrapper->newDataAvailable = false;
	return (isNewDataAvailable);
}