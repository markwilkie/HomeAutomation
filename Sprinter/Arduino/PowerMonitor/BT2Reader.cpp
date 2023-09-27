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

BT2Reader::BT2Reader()
{
	peripheryName="BT-TH-66F94E1C    ";
	txServiceUUID="ffd0";
	txCharacteristicUUID="ffd1";
	rxServiceUUID="fff0";
	rxCharacteristicUUID="fff1";	

	//Init our register data
	registerDescriptionSize = sizeof(registerDescription) / sizeof(registerDescription[0]);
	registerValueSize = 0;
	for (int i = 0; i < registerDescriptionSize; i++) { registerValueSize += (registerDescription[i].bytesUsed / 2); }

	log("Register Description is %d entries, registerValue is %d entries\n", registerDescriptionSize, registerValueSize);

	dataReceivedLength = 0;
	dataError = false;
	registerExpected = 0;
	newDataAvailable = false;

	for (int j = 0; j < registerDescriptionSize; j++) {
		int registerLength = registerDescription[j].bytesUsed / 2;
		int registerAddress = registerDescription[j].address;

		for (int k = 0; k < registerLength; k++) {
			registerValues[j+k].lastUpdateMillis = 0;
			registerValues[j+k].value = 0;
			registerValues[j+k].registerAddress = registerAddress;
			registerAddress++;
		}
	}

	invalidRegister.lastUpdateMillis = 0;
	invalidRegister.value = 0;
}


void BT2Reader::scanCallback(BLEDevice *peripheral,BLE_SEMAPHORE *bleSemaphore)
{
 	if (peripheral->localName() == peripheryName)
	{
		//Set device
		bleDevice=peripheral;

		//Make sure we're not busy
		if(bleSemaphore->waitingForResponse || bleSemaphore->waitingForConnection)
		{
			log("BLE device %s in use when another send attempt was tried\n",bleSemaphore->btDevice->getPerifpheryName());
			return;
		}
		//Set semaphore for connect
		updateSemaphore(bleSemaphore);

		log("Found targeted BT2 device, attempting connection\n");
		memcpy(peripheryAddress,peripheral->address().c_str(),6);
		peripheral->connect();
	} 
}


boolean BT2Reader::connectCallback(BLEDevice *myDevice,BLE_SEMAPHORE* bleSemaphore) 
{
	log("Discovering Tx service: %s \n",txServiceUUID);
	if(myDevice->discoverService(txServiceUUID))
	{
		log("Renogy Tx service %s discovered.  Now looking for charecteristic %s\n",txServiceUUID,txCharacteristicUUID);
		txDeviceCharateristic=myDevice->characteristic(txCharacteristicUUID);
		if(!txDeviceCharateristic)
		{
			logerror("Renogy Tx characteristic not discovered, disconnecting\n");
			myDevice->disconnect();
			connected=false;
			return false;
		}
	} 
	else 
	{
		logerror("Renogy Tx service not discovered, disconnecting\n");
		myDevice->disconnect();
		return false;
	}

	log("Discovering Rx service: %s \n",rxServiceUUID);
	if(myDevice->discoverService(rxServiceUUID))
	{
		log("Renogy Rx service %s discovered.  Now looking for charecteristic %s\n",rxServiceUUID,rxCharacteristicUUID);
		if(rxDeviceCharateristic=myDevice->characteristic(rxCharacteristicUUID))
		{
			if(rxDeviceCharateristic.canSubscribe() && rxDeviceCharateristic.subscribe())
			{
				rxDeviceCharateristic.setEventHandler(BLEWritten, mainNotifyCallback);
			}
			else
			{
				logerror("Renogy Rx characteristic not discovered or cannot be subscribed to, disconnecting\n");
				myDevice->disconnect();
				connected=false;
				return false;
			} 
		}
	} 
	else 
	{
		logerror("Renogy Rx service not discovered, disconnecting\n");
		myDevice->disconnect();
		return false;
	}

	connected=true;
	log("Found all services and characteristics.\n");

	log("Releasing connect semaphore for BLE device %s\n",bleSemaphore->btDevice->getPerifpheryName());
	bleSemaphore->waitingForConnection=false;

	return true;
}

void BT2Reader::disconnectCallback(BLEDevice *myDevice) 
{
	connected=false;
	memset(peripheryAddress, 0, 6);
}

void BT2Reader::notifyCallback(BLEDevice *myDevice, BLECharacteristic *characteristic,BLE_SEMAPHORE* bleSemaphore) 
{
	if (dataError) { return; }									// don't append anything if there's already an error

	//Read data
	dataError = !appendRenogyPacket(characteristic);		// append second or greater packet

	if (!dataError && dataReceivedLength == getExpectedLength(dataReceived)) 
	{
		if (getIsReceivedDataValid(dataReceived)) 
		{
			//Serial.printf("Complete datagram of %d bytes, %d registers (%d packets) received:\n", 
			//	device->dataReceivedLength, device->dataReceived[2], device->dataReceivedLength % 20 + 1);
			//printHex(device->dataReceived, device->dataReceivedLength);
			processDataReceived(bleSemaphore);

			//debug
			uint16_t startRegister = bt2Commands[lastCmdSent].startRegister;
			uint16_t numberOfRegisters = bt2Commands[lastCmdSent].numberOfRegisters;
			Serial.printf("Received response for %d registers 0x%04X - 0x%04X\n",numberOfRegisters,startRegister,startRegister + numberOfRegisters - 1);

			uint8_t bt2Response[21] = "main recv data[XX] [";
			for (int i = 0; i < dataError; i+= 20) 
			{
				bt2Response[15] = HEX_LOWER_CASE[(dataReceived[i] / 16) & 0x0F];
				bt2Response[16] = HEX_LOWER_CASE[(dataReceived[i]) & 0x0F];
				Serial.printf("Sending response #%d to BT2: %s\n", i, bt2Response);
				txDeviceCharateristic.writeValue(bt2Response, 20);
			}
		} 
		else 
		{
			Serial.printf("Checksum error: received is 0x%04X, calculated is 0x%04X\n", 
				getProvidedModbusChecksum(dataReceived), getCalculatedModbusChecksum(dataReceived));
		}
	} 
}

void BT2Reader::sendStartupCommand(BLE_SEMAPHORE* bleSemaphore)
{
	Serial.println("Sending Renogy startup command");
	int cmdIndex=0;
	uint16_t startRegister = bt2Commands[cmdIndex].startRegister;
	uint16_t numberOfRegisters = bt2Commands[cmdIndex].numberOfRegisters;
	uint32_t sendReadCommandTime = millis();
	sendReadCommand(startRegister, numberOfRegisters, bleSemaphore);
	lastCmdSent=cmdIndex;
}

void BT2Reader::sendSolarOrAlternaterCommand(BLE_SEMAPHORE* bleSemaphore)
{
	int cmdIndex=0;
	if(lastCmdSent==4)
	{
		Serial.println("Sending Renogy solar command");
		cmdIndex=5;
	}
	else
	{
		Serial.println("Sending Renogy alternater command");
		cmdIndex=4;
	}

	uint16_t startRegister = bt2Commands[cmdIndex].startRegister;
	uint16_t numberOfRegisters = bt2Commands[cmdIndex].numberOfRegisters;
	uint32_t sendReadCommandTime = millis();
	sendReadCommand(startRegister, numberOfRegisters, bleSemaphore);
	lastCmdSent=cmdIndex;
}



/** Appends received data.  Returns false if there's potential for buffer overrun, true otherwise
 */
boolean BT2Reader::appendRenogyPacket(BLECharacteristic *characteristic) 
{
	int dataLen=characteristic->valueLength();
	if(dataLen<0)
		return true;

	if (dataLen + dataReceivedLength >= DEFAULT_DATA_BUFFER_LENGTH -1) 
	{
		logerror("Buffer overrun receiving data\n");
		return false;
	}

	characteristic->readValue(&dataReceived[dataReceivedLength],dataLen);
	dataReceivedLength += dataLen;
	if (getExpectedLength(dataReceived) < dataReceivedLength) 
	{
		logerror("Buffer overrun receiving data\n");
		return false;
	}
	return true;
}

void BT2Reader::sendReadCommand(uint16_t startRegister, uint16_t numberOfRegisters,BLE_SEMAPHORE* bleSemaphore) 
{
	//Make sure we're clear to send
	if(bleSemaphore->waitingForResponse)
	{
		log("BLE device %s in use when another send attempt was tried\n",bleSemaphore->btDevice->getPerifpheryName());
		return;
	}

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

	txDeviceCharateristic.writeValue(command, 8);
	registerExpected = startRegister;
	dataReceivedLength = 0;
	dataError = false;
	newDataAvailable = false;

	//Update semaphore
	updateSemaphore(bleSemaphore,startRegister);		
}

void BT2Reader::processDataReceived(BLE_SEMAPHORE* bleSemaphore) 
{
	int registerOffset = 0;
	int registersProvided = dataReceived[2] / 2;

	//Check if we should release the semaphore
	if(getRegisterValueIndex(bleSemaphore->expectedBytes))
	{
		log("Releasing response semaphore for BLE device %s\n",bleSemaphore->btDevice->getPerifpheryName());
		bleSemaphore->waitingForResponse=false;
	}
	
	while (registerOffset < registersProvided) {
		int registerIndex = getRegisterValueIndex(registerExpected + registerOffset);
		if (registerIndex >= 0) {
			uint8_t msb = dataReceived[registerOffset * 2 + 3];
			uint8_t lsb = dataReceived[registerOffset * 2 + 4];
			registerValues[registerIndex].value = msb * 256 + lsb;
			registerValues[registerIndex].lastUpdateMillis = millis();
		}
		registerOffset++;
	}	
	newDataAvailable = true;
}

void BT2Reader::updateValues()
{
	int numRegisters=sizeof(registerValues)/(sizeof(registerValues[0]));
	for(int i=0;i<numRegisters;i++)
	{
		int registerDescriptionIndex;
		const REGISTER_DESCRIPTION *rr;
		REGISTER_VALUE registerValue=registerValues[i];
		uint16_t registerAddress=registerValue.registerAddress;
		switch(registerAddress)
		{
			//Alternater amps
			case 0x0101:
				registerDescriptionIndex = getRegisterDescriptionIndex(registerAddress);
				rr = &registerDescription[registerDescriptionIndex];
				alternaterAmps=(float)(registerValue.value) * rr->multiplier;
				Serial.printf("Aux Battery: value=%d multiplier=%f result=%f\n",registerValue.value,rr->multiplier,alternaterAmps);
				break;
			case 0x0105:
				registerDescriptionIndex = getRegisterDescriptionIndex(registerAddress);
				rr = &registerDescription[registerDescriptionIndex];
				alternaterAmps=(float)(registerValue.value) * rr->multiplier;
				Serial.printf("Alternater: value=%d multiplier=%f result=%f\n",registerValue.value,rr->multiplier,alternaterAmps);
				break;
			case 0x0108:
				registerDescriptionIndex = getRegisterDescriptionIndex(registerAddress);
				rr = &registerDescription[registerDescriptionIndex];
				solarAmps=(float)(registerValue.value) * rr->multiplier;
				Serial.printf("Solar: value=%d multiplier=%f result=%f\n",registerValue.value,rr->multiplier,solarAmps);
				break;				
		}
	}
	
	newDataAvailable=false;
}

float BT2Reader::getAlternaterAmps()
{
	return alternaterAmps;
}

float BT2Reader::getSolarAmps()
{
	return solarAmps;
}

void BT2Reader::dumpRenogyData()
{
	int lastCmd=lastCmdSent;
	uint16_t startRegister = bt2Commands[lastCmd].startRegister;
	uint16_t numberOfRegisters = bt2Commands[lastCmd].numberOfRegisters;

	Serial.printf("Received response for %d registers 0x%04X - 0x%04X: ",numberOfRegisters,startRegister,startRegister + numberOfRegisters - 1);
	printHex(dataReceived, dataReceivedLength, false);
	
	for (int i = 0; i < numberOfRegisters; i++) 
	{
		Serial.printf("Register 0x%04X contains %d\n",startRegister + i,getRegister(startRegister + i)->value);
	}

	for (int i = 0; i < numberOfRegisters; i++) 
	{
		printRegister(startRegister + i);
	}
}


int BT2Reader::getRegisterValueIndex(uint16_t registerAddress) {
	int left = 0;
	int right = registerValueSize - 1;
	while (left <= right) {
		int mid = (left + right) / 2;
		if (registerValues[mid].registerAddress == registerAddress) { return mid; }
		if (registerValues[mid].registerAddress < registerAddress) { 
			left = mid + 1;
		} else {
			right = mid -1;
		}
	}
	return -1;
}

int BT2Reader::getRegisterDescriptionIndex(uint16_t registerAddress) {
	int left = 0;
	int right = registerDescriptionSize - 1;
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

REGISTER_VALUE *BT2Reader::getRegister(uint16_t registerAddress) 
{
	int registerValueIndex = getRegisterValueIndex(registerAddress);
	if (registerValueIndex < 0) { return &invalidRegister; }
	return (&(registerValues[registerValueIndex]));
}
