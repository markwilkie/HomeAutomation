#include "BT2Reader.h"

/**	Code for communicating with Renogy DCC series MPPT solar controllers and DC:DC converters using ArduinoBLE.
 * 
 * These include:
 * Renogy DCC30s - https://www.renogy.com/dcc30s-12v-30a-dual-input-dc-dc-on-board-battery-charger-with-mppt/
 * Renogy DCC50s - https://www.renogy.com/dcc50s-12v-50a-dc-dc-on-board-battery-charger-with-mppt/ 
 * 
 * This library can read operating parameters.  While it is possible with some effort to modify this code to
 * write parameters, I have not exposed this here, as it is a security risk.  USE THIS AT YOUR OWN RISK!
 * 
 * Other devices that use the BT-2 could likely use this library too, albeit with additional register lookups
 * Currently this library only supports connecting to one BT2 device, future versions will have support for 
 * multiple connected devices  
 * 
 * Written by Neil Shepherd 2022, ported to ArduinoBLE by Mark Wilkie 2023
 * Released under GPL license
 * 
 * Thanks go to Wireshark for allowing me to read the bluetooth packets used 
 */

extern void mainNotifyCallback(BLEDevice peripheral, BLECharacteristic characteristic);


void BT2Reader::begin(BTDeviceWrapper *_btDevice) 
{
	btDeviceWrapper = _btDevice;

	//Init our register data
	btDeviceWrapper->registerDescriptionSize = sizeof(registerDescription) / sizeof(registerDescription[0]);
	btDeviceWrapper->registerValueSize = 0;
	for (int i = 0; i < btDeviceWrapper->registerDescriptionSize; i++) { btDeviceWrapper->registerValueSize += (registerDescription[i].bytesUsed / 2); }

	log("Register Description is %d entries, registerValue is %d entries\n", btDeviceWrapper->registerDescriptionSize, btDeviceWrapper->registerValueSize);

	btDeviceWrapper->dataReceivedLength = 0;
	btDeviceWrapper->dataError = false;
	btDeviceWrapper->registerExpected = 0;
	btDeviceWrapper->newDataAvailable = false;

	for (int j = 0; j < btDeviceWrapper->registerDescriptionSize; j++) {
		int registerLength = registerDescription[j].bytesUsed / 2;
		int registerAddress = registerDescription[j].address;

		for (int k = 0; k < registerLength; k++) {
			btDeviceWrapper->registerValues[j+k].lastUpdateMillis = 0;
			btDeviceWrapper->registerValues[j+k].value = 0;
			btDeviceWrapper->registerValues[j+k].registerAddress = registerAddress;
			registerAddress++;
		}
	}

	btDeviceWrapper->invalidRegister.lastUpdateMillis = 0;
	btDeviceWrapper->invalidRegister.value = 0;
}


boolean BT2Reader::scanCallback(BLEDevice *peripheral) 
{
 	if (peripheral->localName() == btDeviceWrapper->peripheryName)
	{
		log("Found targeted BT2 device, attempting connection\n");
		peripheral->connect();
		return true;
	} 
				
	//If we got here, something went wrong
	return false;
}


boolean BT2Reader::connectCallback(BLEDevice *myDevice) 
{
	if(myDevice->discoverService(btDeviceWrapper->txServiceUUID))
	{
		log("Renogy Tx service %s discovered\n",btDeviceWrapper->txServiceUUID);
		btDeviceWrapper->txDeviceCharateristic=myDevice->characteristic(btDeviceWrapper->txCharacteristicUUID);
		if(btDeviceWrapper->txDeviceCharateristic)
		{
			logerror("Renogy Tx characteristic not discovered, disconnecting\n");
			myDevice->disconnect();
			btDeviceWrapper->connected=false;
			return true;
		}
	} 
	else 
	{
		logerror("Renogy Tx service not discovered, disconnecting\n");
		myDevice->disconnect();
		return true;
	}

	if(myDevice->discoverService(btDeviceWrapper->rxServiceUUID))
	{
		log("Renogy Rx service %s discovered\n",btDeviceWrapper->rxServiceUUID);
		if(btDeviceWrapper->rxDeviceCharateristic=myDevice->characteristic(btDeviceWrapper->rxCharacteristicUUID))
		{
			if(btDeviceWrapper->rxDeviceCharateristic.canSubscribe() && btDeviceWrapper->rxDeviceCharateristic.subscribe())
			{
				btDeviceWrapper->rxDeviceCharateristic.setEventHandler(BLEWritten, mainNotifyCallback);
			}
			else
			{
				logerror("Renogy Rx characteristic not discovered or cannot be subscribed to, disconnecting\n");
				myDevice->disconnect();
				btDeviceWrapper->connected=false;
				return true;
			} 
		}
	} 
	else 
	{
		logerror("Renogy Rx service not discovered, disconnecting\n");
		myDevice->disconnect();
		return true;
	}

	btDeviceWrapper->connected=true;
	btDeviceWrapper->bleDevice=myDevice;
	return true;
}

void BT2Reader::disconnectCallback(BLEDevice *myDevice) 
{
	btDeviceWrapper->connected=false;
	memset(btDeviceWrapper->peripheryAddress, 0, 6);
}

boolean BT2Reader::notifyCallback(BLEDevice *myDevice, BLECharacteristic *characteristic) 
{
	if (btDeviceWrapper->dataError) { return false; }									// don't append anything if there's already an error

	//Read data
	btDeviceWrapper->dataError = !appendRenogyPacket(characteristic);		// append second or greater packet

	if (!btDeviceWrapper->dataError && btDeviceWrapper->dataReceivedLength == getExpectedLength(btDeviceWrapper->dataReceived)) 
	{
		if (getIsReceivedDataValid(btDeviceWrapper->dataReceived)) 
		{
			//Serial.printf("Complete datagram of %d bytes, %d registers (%d packets) received:\n", 
			//	device->dataReceivedLength, device->dataReceived[2], device->dataReceivedLength % 20 + 1);
			//printHex(device->dataReceived, device->dataReceivedLength);
			processDataReceived();

			uint8_t bt2Response[21] = "main recv data[XX] [";
			for (int i = 0; i < btDeviceWrapper->dataError; i+= 20) 
			{
				bt2Response[15] = HEX_LOWER_CASE[(btDeviceWrapper->dataReceived[i] / 16) & 0x0F];
				bt2Response[16] = HEX_LOWER_CASE[(btDeviceWrapper->dataReceived[i]) & 0x0F];
				Serial.printf("Sending response #%d to BT2: %s\n", i, bt2Response);
				btDeviceWrapper->txDeviceCharateristic.writeValue(bt2Response, 20);
			}
		} 
		else 
		{
			Serial.printf("Checksum error: received is 0x%04X, calculated is 0x%04X\n", 
				getProvidedModbusChecksum(btDeviceWrapper->dataReceived), getCalculatedModbusChecksum(btDeviceWrapper->dataReceived));

			return false;
		}
	} 

	return true;
}


/** Appends received data.  Returns false if there's potential for buffer overrun, true otherwise
 */
boolean BT2Reader::appendRenogyPacket(BLECharacteristic *characteristic) 
{
	int dataLen=characteristic->valueLength();
	if(dataLen<0)
		return true;

	if (dataLen + btDeviceWrapper->dataReceivedLength >= DEFAULT_DATA_BUFFER_LENGTH -1) 
	{
		logerror("Buffer overrun receiving data\n");
		return false;
	}

	characteristic->readValue(&btDeviceWrapper->dataReceived[btDeviceWrapper->dataReceivedLength],dataLen);
	btDeviceWrapper->dataReceivedLength += dataLen;
	if (getExpectedLength(btDeviceWrapper->dataReceived) < btDeviceWrapper->dataReceivedLength) 
	{
		logerror("Buffer overrun receiving data\n");
		return false;
	}
	return true;
}

void BT2Reader::processDataReceived() {

	int registerOffset = 0;
	int registersProvided = btDeviceWrapper->dataReceived[2] / 2;
	
	while (registerOffset < registersProvided) {
		int registerIndex = getRegisterValueIndex(btDeviceWrapper->registerExpected + registerOffset);
		if (registerIndex >= 0) {
			uint8_t msb = btDeviceWrapper->dataReceived[registerOffset * 2 + 3];
			uint8_t lsb = btDeviceWrapper->dataReceived[registerOffset * 2 + 4];
			btDeviceWrapper->registerValues[registerIndex].value = msb * 256 + lsb;
			btDeviceWrapper->registerValues[registerIndex].lastUpdateMillis = millis();
		}
		registerOffset++;
	}	
	btDeviceWrapper->newDataAvailable = true;
}


void BT2Reader::sendReadCommand(uint16_t startRegister, uint16_t numberOfRegisters) 
{
	uint8_t command[20];
	command[0] = 0xFF;
	command[1] = 0x03;
	command[2] = (startRegister >> 8) & 0xFF;
	command[3] = startRegister & 0xFF;
	command[4] = (numberOfRegisters >> 8) & 0xFF;
	command[5] = numberOfRegisters & 0xFF;
	uint16_t checksum = getCalculatedModbusChecksum(command, 0, 6);
	command[6] = checksum & 0xFF;
	command[7] = (checksum >> 8) & 0xFF;

	log("Sending command sequence: ");
	for (int i = 0; i < 8; i++) { logprintf("%02X ", command[i]); }
	logprintf("\n");

	btDeviceWrapper->txDeviceCharateristic.writeValue(command, 8);
	btDeviceWrapper->registerExpected = startRegister;
	btDeviceWrapper->dataReceivedLength = 0;
	btDeviceWrapper->dataError = false;
	btDeviceWrapper->newDataAvailable = false;
}


int BT2Reader::getRegisterValueIndex(uint16_t registerAddress) {
	int left = 0;
	int right = btDeviceWrapper->registerValueSize - 1;
	while (left <= right) {
		int mid = (left + right) / 2;
		if (btDeviceWrapper->registerValues[mid].registerAddress == registerAddress) { return mid; }
		if (btDeviceWrapper->registerValues[mid].registerAddress < registerAddress) { 
			left = mid + 1;
		} else {
			right = mid -1;
		}
	}
	return -1;
}

int BT2Reader::getRegisterDescriptionIndex(uint16_t registerAddress) {
	int left = 0;
	int right = btDeviceWrapper->registerDescriptionSize - 1;
	while (left <= right) {
		int mid = (left + right) / 2;
		if (registerDescription[mid].address == registerAddress) { return mid; }
		if (registerDescription[mid].address < registerAddress) { 
			left = mid + 1;
		} else {
			right = mid -1;
		}
	}
	return -1;
}


int BT2Reader::getExpectedLength(uint8_t * data) { return data[2] + 5; }

REGISTER_VALUE * BT2Reader::getRegister(uint16_t registerAddress) 
{
	int registerValueIndex = getRegisterValueIndex(registerAddress);
	if (registerValueIndex < 0) { return &btDeviceWrapper->invalidRegister; }
	return (&(btDeviceWrapper->registerValues[registerValueIndex]));
	}

boolean BT2Reader::getIsNewDataAvailable() 
{
	boolean isNewDataAvailable = btDeviceWrapper->newDataAvailable;
	btDeviceWrapper->newDataAvailable = false;
	return (isNewDataAvailable);
}
