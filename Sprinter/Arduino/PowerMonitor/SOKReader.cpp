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

void SOKReader::scanCallback(BLEDevice *peripheral) 
{
	if (peripheral->localName() == peripheryName)
	{
		Serial.println("Found targeted SOK device, attempting connection");
		memcpy(peripheryAddress,peripheral->address().c_str(),6);
		peripheral->connect();
	} 
}


boolean SOKReader::connectCallback(BLEDevice *myDevice) 
{
	Serial.printf("SOK: Discovering Tx service: %s\n",txServiceUUID);
	if(myDevice->discoverService(txServiceUUID))
	{
		Serial.printf("SOK Tx service %s discovered\n",txServiceUUID);
		txDeviceCharateristic=myDevice->characteristic(txCharacteristicUUID);
		if(!txDeviceCharateristic)
		{
			Serial.printf("ERROR: SOK Tx characteristic not discovered, disconnecting\n");
			myDevice->disconnect();
			connected=false;
			return false;
		}
	} 
	else 
	{
		Serial.println("ERROR: SOK Tx service not discovered, disconnecting");
		myDevice->disconnect();
		connected=false;
		return false;		
	}

	Serial.printf("SOK: Discovering Rx service: %s\n",rxServiceUUID);
	Serial.println(txServiceUUID);
	if(myDevice->discoverService(rxServiceUUID))
	{
		Serial.printf("SOK Rx service %s discovered.  Now looking for characteristic %s\n",rxServiceUUID,rxCharacteristicUUID);
		if(rxDeviceCharateristic=myDevice->characteristic(rxCharacteristicUUID))
		{
			if(rxDeviceCharateristic.canSubscribe() && rxDeviceCharateristic.subscribe())
			{
				rxDeviceCharateristic.setEventHandler(BLEWritten, mainNotifyCallback);
			}
			else
			{
				Serial.println("ERROR: SOK Rx characteristic not discovered or cannot be subscribed to, disconnecting");
				myDevice->disconnect();
				connected=false;
				return false;
			} 
		}
	} 
	else 
	{
		Serial.println("ERROR: SOK Rx service not discovered, disconnecting\n");
		myDevice->disconnect();
		connected=false;
		return false;
	}

	connected=true;
	bleDevice=myDevice;
	Serial.println("SOK found all needed services and characteristics.");
	return true;
}

void SOKReader::disconnectCallback(BLEDevice *myDevice) 
{
	connected=false;
	memset(peripheryAddress, 0, 6);
}

void SOKReader::notifyCallback(BLEDevice *myDevice, BLECharacteristic *characteristic) 
{
	int dataLen=characteristic->valueLength();
	characteristic->readValue(dataReceived,dataLen);
	dataReceivedLength = dataLen;
	newDataAvailable=true;
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

	txDeviceCharateristic.writeValue(command, 6);
	dataReceivedLength = 0;
	dataError = false;
	newDataAvailable = false;
}

void SOKReader::updateValues()
{
	if(dataReceived[0]==0xCC && dataReceived[1]==0xF0)
    {
		soc=bytesToInt(dataReceived+16,1,false);
		volts=bytesToInt(dataReceived+2,3,false)*.001;
		amps=bytesToInt(dataReceived+5,3,true)*.001;
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

		
		
