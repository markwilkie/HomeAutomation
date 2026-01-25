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
	//logger.log(INFO, "SOK %d: disconnectCallback - Device disconnected", batteryNumber);
	//logger.log(INFO, "SOK %d:   Previous state: connected=%s, bleClient=%s", 
	//	batteryNumber, connected ? "true" : "false", bleClient ? "valid" : "null");
	
	connected=false;
	memset(peripheryAddress, 0, 6);
	bleClient = nullptr;
	
	//logger.log(INFO, "SOK %d:   State cleared, ready for reconnection", batteryNumber);
}

/*
 * notifyCallback() - Handle BLE notification from SOK battery BMS
 * 
 * Called when SOK battery sends response data via BLE notification.
 * Runs in BLE task context - keep processing minimal!
 * 
 * SOK RESPONSE PACKET FORMAT:
 * Bytes 0-1: Packet marker (identifies packet type)
 *   - 0xCC 0xF0: Base packet (SOC, voltage, current, capacity, cycles)
 *   - 0xCC 0xF2: CMOS/DMOS status (from C1 command)
 *   - 0xCC 0xF3: Heating status (from C2 command)
 *   - 0xCC 0xF9: Protection status (from C4 command)
 * 
 * DUAL-BUFFER STRATEGY:
 * The base packet (0xF0) with SOC data goes to basePacketData[] buffer.
 * Secondary packets (0xF2, 0xF3, 0xF9) go to dataReceived[] buffer.
 * This prevents secondary packets from overwriting SOC data.
 * 
 * TWO-PACKET COMMANDS (C1, C2):
 * These commands return two packets that can arrive in any order:
 * - 0xF0 base packet + 0xF2 (C1) or 0xF3 (C2) secondary packet
 * Semaphore is only released when BOTH packets are received.
 */
void SOKReader::notifyCallback(NimBLERemoteCharacteristic *characteristic, uint8_t *pData, size_t length, BLE_SEMAPHORE *bleSemaphore) 
{
	// Minimal processing - this runs in BLE task context with limited stack
	newDataAvailable=true;

	// Check what packet we received
	uint16_t receivedMarker = pData[0] | (pData[1]<<8);
	
	// Store in appropriate buffer - base packet (0xF0) goes to separate buffer to preserve SOC
	if(receivedMarker == 0xF0CC)  // 0xCC 0xF0 - base packet with SOC
	{
		memcpy(basePacketData, pData, length);
		basePacketLength = length;
	}
	else
	{
		// Secondary packets (0xF2, 0xF3, 0xF9) go to dataReceived
		memcpy(dataReceived, pData, length);
		dataReceivedLength = length;
	}
	
	// For C1/C2: need both 0xF0 (base) and 0xF2/0xF3 (secondary) packets - can arrive in any order
	// For C4: only one packet (0xF9)
	if(expectedSecondPacket != 0)
	{
		// Waiting for two packets (C1 or C2 command)
		if(bleSemaphore->expectedBytes == receivedMarker)
		{
			receivedFirstPacket = true;  // Got the 0xF0 base packet
		}
		else if(expectedSecondPacket == receivedMarker)
		{
			receivedSecondPacket = true;  // Got the secondary packet (0xF2 or 0xF3)
		}
		
		// Release semaphore when we have both packets (regardless of order)
		if(receivedFirstPacket && receivedSecondPacket)
		{
			bleSemaphore->waitingForResponse = false;
			receivedFirstPacket = false;
			receivedSecondPacket = false;
		}
	}
	else
	{
		// Single packet command (C4)
		if(bleSemaphore->expectedBytes == receivedMarker)
		{
			bleSemaphore->waitingForResponse = false;
		}
	}
}

/*
 * bytesToInt() - Convert raw bytes to integer (little-endian)
 * 
 * SOK battery sends multi-byte values in little-endian format.
 * This function assembles bytes into an integer.
 * 
 * PARAMETERS:
 * @param bytes - Pointer to byte array
 * @param len - Number of bytes to process (1-4)
 * @param isSigned - If true, interpret as signed 2's complement
 * 
 * SIGNED CONVERSION:
 * For signed values (like current which can be negative for discharge):
 * 1. Check sign bit (MSB of final byte)
 * 2. If set, sign-extend to 32 bits
 * Example: 3-byte value 0xFFFF80 (-128) becomes 0xFFFFFF80
 * 
 * RETURNS: Integer value (signed or unsigned based on isSigned)
 */
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


/*
 * sendReadCommand() - Send data request command to SOK battery BMS
 * 
 * SOK batteries use a simple 6-byte command protocol:
 * Byte 0: 0xEE - Start marker
 * Byte 1: Command code (0xC0, 0xC1, 0xC2, or 0xC4)
 * Bytes 2-4: 0x00 0x00 0x00 - Padding
 * Byte 5: Checksum
 * 
 * COMMAND CODES AND RESPONSES:
 * - 0xC1: Returns base data (0xF0) + CMOS/DMOS status (0xF2)
 * - 0xC2: Returns base data (0xF0) + heating status (0xF3)
 * - 0xC4: Returns protection status (0xF9) only
 * 
 * CYCLING STRATEGY:
 * Commands alternate C1 -> C2 -> C1 -> ... with periodic C4 for protection.
 * This ensures all data types are refreshed regularly.
 * C1/C2 commands return TWO packets; semaphore tracks both.
 * 
 * PROTECTION_COUNT controls how often C4 is sent (every N cycles).
 */
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
	receivedFirstPacket = false;   // Reset packet tracking
	receivedSecondPacket = false;  // Reset packet tracking

	//Update semaphore - set expected response based on command sent
	uint16_t expectedBytes;
	if(command[1] == 0xc4) {
		expectedBytes = 0xCC | (0xF9 << 8);  // C4 returns only 0xCC 0xF9
		expectedSecondPacket = 0;             // No second packet for C4
	} else if(command[1] == 0xc1) {
		expectedBytes = 0xCC | (0xF0 << 8);  // First packet: base data
		expectedSecondPacket = 0xCC | (0xF2 << 8);  // Second packet: CMOS/DMOS
	} else {
		// C2 command
		expectedBytes = 0xCC | (0xF0 << 8);  // First packet: base data
		expectedSecondPacket = 0xCC | (0xF3 << 8);  // Second packet: heating
	}
	updateSemaphore(bleSemaphore, expectedBytes);
	// Verbose logging removed - uncomment for debugging
	// logger.log(INFO, "SOK %d:   Semaphore updated, expecting response marker: %04X", batteryNumber, expectedBytes);		
}

bool SOKReader::isCurrent()
{
	return (millis()-lastHeardTime)<SOK_BLE_STALE;
}

void SOKReader::resetStale()
{
	lastHeardTime = millis();
}

/*
 * updateValues() - Parse received packets and update battery state variables
 * 
 * Called from main loop when getIsNewDataAvailable() returns true.
 * Parses both basePacketData[] (0xF0) and dataReceived[] (0xF2/F3/F9).
 * 
 * BASE PACKET (0xF0) LAYOUT:
 * - Bytes 2-4: Voltage (3 bytes, ÷1000 for volts)
 * - Bytes 5-7: Current (3 bytes, signed, ÷1000 for amps, negative=discharge)
 * - Bytes 11-13: Remaining capacity (3 bytes, ÷1000 for Ah)
 * - Bytes 14-15: Cycle count (2 bytes)
 * - Byte 16: State of charge (0-100%)
 * 
 * SECONDARY PACKETS:
 * - 0xF2 (from C1): CMOS flag (byte 2), DMOS flag (byte 3), temperature (bytes 5-6)
 * - 0xF3 (from C2): Heating flag (byte 8)
 * - 0xF9 (from C4): Protection flags (bytes 2-16, any non-zero = protected)
 * 
 * CMOS = Charge MOSFET, DMOS = Discharge MOSFET
 * Both should be ON (true) for normal operation.
 */
void SOKReader::updateValues()
{
	// Parse base packet (0xF0) from separate buffer - contains SOC, volts, amps, etc.
	if(basePacketLength > 0 && basePacketData[0]==0xCC && basePacketData[1]==0xF0)
    {
		lastHeardTime=millis();

		soc=bytesToInt(basePacketData+16,1,false);
		volts=bytesToInt(basePacketData+2,3,false)*.001;
		amps=bytesToInt(basePacketData+5,3,true)*.001;
		capacity=bytesToInt(basePacketData+11,3,true)*.001;
		cycles=bytesToInt(basePacketData+14,2,false);

		//debug
		//logger.log("Loop:  (cycles?): %d",cycles);
	}

	// Parse secondary packets from dataReceived buffer
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
		
		
