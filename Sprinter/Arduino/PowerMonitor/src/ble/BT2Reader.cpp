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
	lastHeardTime=millis();

	//Init our register data
	registerDescriptionSize = sizeof(registerDescription) / sizeof(registerDescription[0]);
	registerValueSize = 0;
	for (int i = 0; i < registerDescriptionSize; i++) { registerValueSize += (registerDescription[i].bytesUsed / 2); }

	//logger.log(INFO,"Register Description is %d entries, registerValue is %d entries", registerDescriptionSize, registerValueSize);

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
		//Already connected? Skip
		if(connected)
			return;

		//Set device
		bleDevice=peripheral;

		//Make sure we're not busy
		if(bleSemaphore->waitingForResponse || bleSemaphore->waitingForConnection)
		{
			logger.log(INFO,"BLE device %s in use when another send attempt was tried",bleSemaphore->btDevice->getPerifpheryName());
			return;
		}
		//Set semaphore for connect
		updateSemaphore(bleSemaphore);

		logger.log(INFO,"Found targeted BT2 device, attempting connection");
		memcpy(peripheryAddress,peripheral->address().c_str(),6);
		peripheral->connect();
	} 
}


boolean BT2Reader::connectCallback(BLEDevice *myDevice,BLE_SEMAPHORE* bleSemaphore) 
{
	logger.log(INFO,"Discovering Tx service: %s ",txServiceUUID);
	if(myDevice->discoverService(txServiceUUID))
	{
		logger.log(INFO,"Renogy Tx service %s discovered.  Now looking for charecteristic %s",txServiceUUID,txCharacteristicUUID);
		txDeviceCharateristic=myDevice->characteristic(txCharacteristicUUID);
		if(!txDeviceCharateristic)
		{
			logger.log(ERROR,"Renogy Tx characteristic not discovered, disconnecting");
			myDevice->disconnect();
			connected=false;
			return false;
		}
	} 
	else 
	{
		logger.log(ERROR,"Renogy Tx service not discovered, disconnecting");
		myDevice->disconnect();
		return false;
	}

	logger.log(INFO,"Discovering Rx service: %s ",rxServiceUUID);
	if(myDevice->discoverService(rxServiceUUID))
	{
		logger.log(INFO,"Renogy Rx service %s discovered.  Now looking for charecteristic %s",rxServiceUUID,rxCharacteristicUUID);
		if(rxDeviceCharateristic=myDevice->characteristic(rxCharacteristicUUID))
		{
			if(rxDeviceCharateristic.canSubscribe() && rxDeviceCharateristic.subscribe())
			{
				rxDeviceCharateristic.setEventHandler(BLEWritten, mainNotifyCallback);
			}
			else
			{
				logger.log(ERROR,"Renogy Rx characteristic not discovered or cannot be subscribed to, disconnecting");
				myDevice->disconnect();
				connected=false;
				return false;
			} 
		}
	} 
	else 
	{
		logger.log(ERROR,"Renogy Rx service not discovered, disconnecting");
		myDevice->disconnect();
		return false;
	}

	connected=true;
	lastHeardTime=millis();
	logger.log(INFO,"Found all services and characteristics.");

	bleSemaphore->waitingForConnection=false;
	logger.log(INFO,"Releasing connect semaphore for BLE device %s",bleSemaphore->btDevice->getPerifpheryName());

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
			//logger.log(INFO,"Complete datagram of %d bytes, %d registers (%d packets) received:", 
			//	device->dataReceivedLength, device->dataReceived[2], device->dataReceivedLength % 20 + 1);
			//printHex(device->dataReceived, device->dataReceivedLength);
			processDataReceived(bleSemaphore);

			//debug
			uint16_t startRegister = bt2Commands[lastCmdSent].startRegister;
			uint16_t numberOfRegisters = bt2Commands[lastCmdSent].numberOfRegisters;
			//logger.log(INFO,"Received response for %d registers 0x%04X - 0x%04X",numberOfRegisters,startRegister,startRegister + numberOfRegisters - 1);

			uint8_t bt2Response[21] = "main recv data[XX] [";
			for (int i = 0; i < dataError; i+= 20) 
			{
				bt2Response[15] = HEX_LOWER_CASE[(dataReceived[i] / 16) & 0x0F];
				bt2Response[16] = HEX_LOWER_CASE[(dataReceived[i]) & 0x0F];
				//logger.log(INFO,"Sending response #%d to BT2: %s", i, bt2Response);
				txDeviceCharateristic.writeValue(bt2Response, 20);
			}
		} 
		else 
		{
			logger.log(WARNING,"Checksum error: received is %d, calculated is %d", 
				getProvidedModbusChecksum(dataReceived), getCalculatedModbusChecksum(dataReceived));
		}
	} 
}

void BT2Reader::sendStartupCommand(BLE_SEMAPHORE* bleSemaphore)
{
	logger.log(INFO,"Sending Renogy startup command");
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
		//logger.log(INFO,"Sending Renogy solar command");
		cmdIndex=5;
	}
	else
	{
		//logger.log(INFO,"Sending Renogy alternater command");
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
		logger.log(ERROR,"Buffer overrun receiving data in BT2Reader");
		return false;
	}

	characteristic->readValue(&dataReceived[dataReceivedLength],dataLen);
	dataReceivedLength += dataLen;
	if (getExpectedLength(dataReceived) < dataReceivedLength) 
	{
		logger.log(ERROR,"Unexpected data length in BT2Reader");
		return false;
	}
	return true;
}

void BT2Reader::sendReadCommand(uint16_t startRegister, uint16_t numberOfRegisters,BLE_SEMAPHORE* bleSemaphore) 
{
	//Make sure we're clear to send
	if(bleSemaphore->waitingForResponse)
	{
		logger.log(INFO,"BLE device %s in use when another send attempt was tried",bleSemaphore->btDevice->getPerifpheryName());
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

	//Serial prints are here because it's tough for me to send hex to the serial monitor  (plus, it's for debugging only)
	#ifdef SERIALLOGGER
	Serial.print("Sending command sequence: ");
	for (int i = 0; i < 8; i++) { Serial.printf("%02X ", command[i]); }
	Serial.println();
	#endif

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
		//logger.log(INFO,"Releasing response semaphore for BLE device %s",bleSemaphore->btDevice->getPerifpheryName());
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

bool BT2Reader::isCurrent()
{
	return (millis()-lastHeardTime)<BT2_BLE_STALE;
}

void BT2Reader::updateValues()
{
	int numRegisters=sizeof(registerValues)/(sizeof(registerValues[0]));
	for(int i=0;i<numRegisters;i++)
	{
		lastHeardTime=millis();

		int registerDescriptionIndex;
		const REGISTER_DESCRIPTION *rr;
		REGISTER_VALUE registerValue=registerValues[i];
		uint16_t registerAddress=registerValue.registerAddress;
		switch(registerAddress)
		{
			//Alternater amps
			case RENOGY_AUX_BATT_VOLTAGE:
				registerDescriptionIndex = getRegisterDescriptionIndex(registerAddress);
				rr = &registerDescription[registerDescriptionIndex];
				alternaterAmps=(float)(registerValue.value) * rr->multiplier;
				//logger.log(INFO,"Aux Battery: value=%d multiplier=%f result=%f",registerValue.value,rr->multiplier,alternaterAmps);
				break;
			case RENOGY_ALTERNATOR_CURRENT:
				registerDescriptionIndex = getRegisterDescriptionIndex(registerAddress);
				rr = &registerDescription[registerDescriptionIndex];
				alternaterAmps=(float)(registerValue.value) * rr->multiplier;
				//logger.log(INFO,"Alternater: value=%d multiplier=%f result=%f",registerValue.value,rr->multiplier,alternaterAmps);
				break;
			case RENOGY_SOLAR_CURRENT:
				registerDescriptionIndex = getRegisterDescriptionIndex(registerAddress);
				rr = &registerDescription[registerDescriptionIndex];
				solarAmps=(float)(registerValue.value) * rr->multiplier;
				//logger.log(INFO,"Solar: value=%d multiplier=%f result=%f",registerValue.value,rr->multiplier,solarAmps);
				break;
			case RENOGY_TODAY_AMP_HOURS:
				registerDescriptionIndex = getRegisterDescriptionIndex(registerAddress);
				rr = &registerDescription[registerDescriptionIndex];
				ampHours=(float)(registerValue.value) * rr->multiplier;
				//logger.log(INFO,"Today AH: value=%d multiplier=%f result=%f",registerValue.value,rr->multiplier,ampHours);
				break;	
			case RENOGY_AUX_BATT_TEMPERATURE:
				uint8_t msb = (registerValue.value >> 8) & 0xFF;
				uint8_t lsb = (registerValue.value) & 0xFF;
				//logger.log(INFO,"Temperatures: ",false)				;
				//logger.log((lsb & 0x80) > 0 ? '-' : '+',false);  //aux batt temp
				//logger.log(INFO,"%d C, ", lsb & 0x7F);
				//logger.log((msb & 0x80) > 0 ? '-' : '+',false);  //controller temp
				//logger.log(INFO,"%d C, ", msb & 0x7F); 

				temperature=msb & 0x7F;
				if((msb & 0x80) > 0)
					temperature=temperature*-1;
				//logger.log(INFO,"Temperature: %fC",temperature);
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

float BT2Reader::getTemperature()
{
	return temperature;
}

void BT2Reader::dumpRenogyData()
{
	#ifdef SERIALLOGGER
	int lastCmd=lastCmdSent;
	uint16_t startRegister = bt2Commands[lastCmd].startRegister;
	uint16_t numberOfRegisters = bt2Commands[lastCmd].numberOfRegisters;

	Serial.printf("Received response for %d registers 0x%04X - 0x%04X: ",numberOfRegisters,startRegister,startRegister + numberOfRegisters - 1);
	printHex(dataReceived, dataReceivedLength, false);
	
	for (int i = 0; i < numberOfRegisters; i++) 
	{
		Serial.printf("Register 0x%04X contains %d",startRegister + i,getRegister(startRegister + i)->value);
	}

	for (int i = 0; i < numberOfRegisters; i++) 
	{
		printRegister(startRegister + i);
	}
	#endif
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
