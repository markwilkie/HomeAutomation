#include <ArduinoBLE.h>
#include "BT2Reader.h"
#include "SOKReader.h"
#include "PowerMonitor.h"

#define POLL_TIME_MS	5000

const char *RENOGY_DEVICE_NAME = "BT-TH-66F94E1C    ";
const char *SOK_BATTERY_NAME = "SOK-AA12487";

// Renogy Tx and Rx service
const char* RENOGY_TX_SVC_UUID="ffd0";
const char* RENOGY_TX_CHAR_UUID="ffd1";	
const char* RENOGY_RX_SVC_UUID="fff0";	
const char* RENOGY_RX_CHAR_UUID="fff1";	

// SOK Tx and Rx service
const char* SOK_TX_SVC_UUID="ffe0";
const char* SOK_TX_CHAR_UUID="ffe2";	
const char* SOK_RX_SVC_UUID="ffe0";	
const char* SOK_RX_CHAR_UUID="ffe1";	

//Objects to handle connection
const int numOfTargetedDevices=2;
BTDeviceWrapper *targetedDevices[numOfTargetedDevices];
BTDeviceWrapper renogyDeviceWrapper;   //Wrapper for actual connection and meta data (one for each peripheral)
BTDeviceWrapper sokDeviceWrapper;

//Handles reading from the BT2 Renogy device
BT2Reader bt2Reader;
SOKReader sokReader;

//state
int renogyCmdSequenceToSend=0;
boolean tiktok=true;


void setup() 
{
	Serial.begin(115200);
	delay(3000);
	Serial.println("ArduinoBLE (via ESP32-S3) connecting to Renogy BT-2 and SOK battery");
	Serial.println("-----------------------------------------------------\n");
	if (!BLE.begin()) 
	{
		Serial.println("starting Bluetooth® Low Energy module failed!");
		while (1) {delay(1000);};
	}

	//Set callback functions
	BLE.setEventHandler(BLEDiscovered, scanCallback);
	BLE.setEventHandler(BLEConnected, connectCallback);
	BLE.setEventHandler(BLEDisconnected, disconnectCallback);	

	//Add our two targets
	renogyDeviceWrapper.init(RENOGY_DEVICE_NAME,RENOGY_TX_SVC_UUID,RENOGY_TX_CHAR_UUID,RENOGY_RX_SVC_UUID,RENOGY_RX_CHAR_UUID);
	sokDeviceWrapper.init(SOK_BATTERY_NAME,SOK_TX_SVC_UUID,SOK_TX_CHAR_UUID,SOK_RX_SVC_UUID,SOK_RX_CHAR_UUID);
	targetedDevices[0]=&renogyDeviceWrapper;
	targetedDevices[1]=&sokDeviceWrapper;
	
	//Setup Renogy BT2
	bt2Reader.setLoggingLevel(BT2READER_VERBOSE);
	bt2Reader.begin(&renogyDeviceWrapper);
	sokReader.begin(&sokDeviceWrapper);

	// start scanning for peripherals
	delay(2000);
	Serial.println("About to start scanning");	
	BLE.scan();  //scan with dupliates
}

/** Scan callback method.  
 */
void scanCallback(BLEDevice peripheral) 
{
    // discovered a peripheral, print out address, local name, and advertised service
	Serial.printf("Found device %s at %s with uuuid %s\n",peripheral.localName().c_str(),peripheral.address().c_str(),peripheral.advertisedServiceUuid().c_str());

	//Checking with both device handlers to see if this is something they're interested in
	bt2Reader.scanCallback(&peripheral);
	sokReader.scanCallback(&peripheral);
}


/** Connect callback method.  Exact order of operations is up to the user.  if bt2Reader attempted a connection
 * it returns true (whether or not it succeeds).  Usually you can then skip any other code in this connectCallback
 * method, because it's unlikely to be relevant for other possible connections, saving CPU cycles
 */ 
void connectCallback(BLEDevice peripheral) 
{
	if(peripheral.localName()==RENOGY_DEVICE_NAME)
	{
		if (bt2Reader.connectCallback(&peripheral)) 
		{
			Serial.print("Connected to BT device ");
			Serial.println(peripheral.localName());
		}
	}

	if(peripheral.localName()==SOK_BATTERY_NAME)
	{
		if (sokReader.connectCallback(&peripheral)) 
		{
			Serial.print("Connected to SOK device ");
			Serial.println(peripheral.localName());
		}
	}	

	//Should I stop scanning now?
	boolean stopScanning=true;
	for(int i=0;i<numOfTargetedDevices;i++)
	{
		if(!targetedDevices[i]->connected)
			stopScanning=false;
	}
	if(stopScanning)
	{
		Serial.println("All devices connected.  Stopping scan");
		BLE.stopScan();
	}
}

void disconnectCallback(BLEDevice peripheral) 
{
	if(peripheral.localName()==RENOGY_DEVICE_NAME)
	{	
		renogyCmdSequenceToSend=0;  //need to start at the beginning since we disconnected
		bt2Reader.disconnectCallback(&peripheral);

		Serial.print("Disconnected BT2 device: ");
		Serial.print(peripheral.localName());
	}

	if(peripheral.localName()==SOK_BATTERY_NAME)
	{	
		Serial.print("Disconnected SOK device: ");
		Serial.print(peripheral.localName());
	}	

	Serial.println("  Starting scan again");
	BLE.scan();
}

void mainNotifyCallback(BLEDevice peripheral, BLECharacteristic characteristic)
{
	if(peripheral.localName()==RENOGY_DEVICE_NAME)
	{		
		if (bt2Reader.notifyCallback(&peripheral,&characteristic)) 
		{
			Serial.print("Characteristic notify from ");
			Serial.println(peripheral.localName());
		}
	}

	if(peripheral.localName()==SOK_BATTERY_NAME)
	{		
		sokReader.notifyCallback(&peripheral,&characteristic); 
		Serial.print("Characteristic notify from ");
		Serial.println(peripheral.localName());
	}	
}

void handleRenogy()
{
	Serial.print("----------------  Sequence: ");
	Serial.println(renogyCmdSequenceToSend);
	uint16_t startRegister = bt2Commands[renogyCmdSequenceToSend].startRegister;
	uint16_t numberOfRegisters = bt2Commands[renogyCmdSequenceToSend].numberOfRegisters;
	uint32_t sendReadCommandTime = millis();
	bt2Reader.sendReadCommand(startRegister, numberOfRegisters);
	renogyDeviceWrapper.lastCmdSent=renogyCmdSequenceToSend;
	renogyCmdSequenceToSend++;
	if(renogyCmdSequenceToSend>7)  renogyCmdSequenceToSend=4;

	while (!bt2Reader.getIsNewDataAvailable() && (millis() - sendReadCommandTime < 5000)) 
	{
		BLE.poll();
		delay(2);
	}

	if (millis() - sendReadCommandTime >= 5000) 
	{
		Serial.println("Timeout error; did not receive response from BT2 within 5 seconds");
	} 
	else 
	{
		Serial.printf("Received response for %d registers 0x%04X - 0x%04X in %dms: ", 
				numberOfRegisters,
				startRegister,
				startRegister + numberOfRegisters - 1,
				(millis() - sendReadCommandTime)
		);
		bt2Reader.printHex(renogyDeviceWrapper.dataReceived, renogyDeviceWrapper.dataReceivedLength, false);

		
		for (int i = 0; i < numberOfRegisters; i++) 
		{
			Serial.printf("Register 0x%04X contains %d\n",
				startRegister + i,
				bt2Reader.getRegister(startRegister + i)->value
			);
		}

		for (int i = 0; i < numberOfRegisters; i++) 
		{
			bt2Reader.printRegister(startRegister + i);
		}
	}	

}

void readRenogyData()
{
	int lastCmd=renogyDeviceWrapper.lastCmdSent;
	uint16_t startRegister = bt2Commands[lastCmd].startRegister;
	uint16_t numberOfRegisters = bt2Commands[lastCmd].numberOfRegisters;

	Serial.printf("Received response for %d registers 0x%04X - 0x%04X: ", 
		numberOfRegisters,
		startRegister,
		startRegister + numberOfRegisters - 1);
	bt2Reader.printHex(renogyDeviceWrapper.dataReceived, renogyDeviceWrapper.dataReceivedLength, false);

	
	for (int i = 0; i < numberOfRegisters; i++) 
	{
		Serial.printf("Register 0x%04X contains %d\n",
			startRegister + i,
			bt2Reader.getRegister(startRegister + i)->value
		);
	}

	for (int i = 0; i < numberOfRegisters; i++) 
	{
		bt2Reader.printRegister(startRegister + i);
	}
}

void handleSok()
{
	sokReader.sendReadCommand();
}

void readSokData()
{
	//Parse
	uint8_t *data=sokDeviceWrapper.dataReceived;
	if(data[0]==0xCC && data[1]==0xF0)
	{
		int soc=sokReader.bytesToInt(data+16,1,false);
		float volts=sokReader.bytesToInt(data+2,3,false)*.001;
		float amps=sokReader.bytesToInt(data+5,3,true)*.001;
		Serial.print("SOC: ");   Serial.println(soc);
		//lcd.setCursor(0,20);
		//lcd.printf("SOC: %d", soc);
		Serial.print("Volts: ");   Serial.println(volts);
		//lcd.setCursor(0,40);
		//lcd.printf("Volts: %f", volts);        
		Serial.print("Amps: ");   Serial.println(amps);
		//lcd.setCursor(0,60);
		//lcd.printf("Amps: %f", amps);             
	}
	Serial.println("----------------");
}

void loop() 
{
	//required for the ArduinoBLE library
	BLE.poll();

	//Time to ask for data again?
	if (tiktok && renogyDeviceWrapper.connected && millis()>renogyDeviceWrapper.lastCheckedTime+POLL_TIME_MS) 
	{
		renogyDeviceWrapper.lastCheckedTime=millis();
		handleRenogy();
	}

	if (tiktok && sokDeviceWrapper.connected && millis()>sokDeviceWrapper.lastCheckedTime+POLL_TIME_MS) 
	{
		sokDeviceWrapper.lastCheckedTime=millis();
		handleSok();
	}

	//Do we have any data waiting?
	if(bt2Reader.getIsNewDataAvailable())
	{
		readRenogyData();
	}

	if(sokReader.getIsNewDataAvailable())
	{
		readSokData();
	}

	//toggle to the other device
	tiktok=!tiktok;
}