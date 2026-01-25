#ifndef SOK_READER_H
#define SOK_READER_H

/*
 * SOKReader - BLE Reader for SOK Battery BMS
 * 
 * Communicates with SOK (Shenzhen OK) LiFePO4 battery BMS via BLE.
 * 
 * BLE CHARACTERISTICS:
 * - Service UUID: 0000fff0-0000-1000-8000-00805f9b34fb
 * - TX (write):   0000fff1-0000-1000-8000-00805f9b34fb (send commands to BMS)
 * - RX (notify):  0000fff2-0000-1000-8000-00805f9b34fb (receive data from BMS)
 * 
 * PROTOCOL:
 * - Commands: 0x01C1 (basic data) or 0x01C2 (extended data)
 * - Each command generates TWO response packets:
 *   1. First packet: 0xF0 header with basic cell/SOC data
 *   2. Second packet: 0xC1/0xC2 header with voltage/current/capacity
 * - Both packets must be received to have complete data
 * 
 * DATA AVAILABLE:
 * - State of Charge (SOC) - percentage
 * - Voltage - total pack voltage
 * - Current - charge/discharge current (positive = charging)
 * - Capacity - remaining amp-hours
 * - Temperature - battery temperature
 * - CMOS/DMOS status - charge/discharge MOSFET states
 * - Protection flags - overcurrent, overvoltage, etc.
 * - Heater status - battery heater on/off
 * 
 * See README.md for detailed protocol documentation.
 */

#include "BTDevice.hpp"
#include <NimBLEDevice.h>
#include "../logging/logger.h"

extern Logger logger;

#define SOK_BLE_STALE 120000     // Data considered stale after 2 minutes without update
#define PROTECTION_COUNT 50      // Number of protection flags in BMS data

class SOKReader : public BTDevice
{

public:

	SOKReader(const char* _peripheryName, int batteryNumber);

	void scanCallback(NimBLEAdvertisedDevice *myDevice, BLE_SEMAPHORE *bleSemaphore);
	boolean connectCallback(NimBLEClient *myClient, BLE_SEMAPHORE* bleSemaphor);
	void notifyCallback(NimBLERemoteCharacteristic *characteristic, uint8_t *pData, size_t length, BLE_SEMAPHORE* bleSemaphor);
	void disconnectCallback(NimBLEClient *myClient);

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
	void resetStale();

private:

	int bytesToInt(uint8_t *bytes, int len, boolean isSigned) ;	

	int sendCommandCounter=0;
	int batteryNumber=-1;
	uint16_t expectedSecondPacket=0;  // Track second packet for C1/C2 commands
	bool receivedFirstPacket=false;   // Track if first packet received
	bool receivedSecondPacket=false;  // Track if second packet received
	uint8_t basePacketData[128];      // Separate buffer for 0xF0 base packet (contains SOC)
	size_t basePacketLength=0;

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