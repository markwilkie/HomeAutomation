#include <ArduinoBLE.h>
#include <LovyanGFX.hpp>
#include "LGFX_ST32-SC01Plus.hpp"
#include "BT2Reader.h"
#include "SOKReader.h"
#include "PowerMonitor.h"

#include "Payload.h"
#include "Screen.h"
#include "LinearMeter.h"
#include "CircularMeter.h"

#define POLL_TIME_MS	5000
#define SCR_UPDATE_TIME 1000

//Screen lib
LGFX lcd; 

//Screen
Screen screen;

//Meters
CircularMeter centerOutMeter;
CircularMeter centerInMeter;
LinearMeter socMeter;
LinearMeter waterMeter;

//Dynamic text
Text battHoursLeft;
Text waterDaysLeft;
Text volts;
Text hertz;

//Objects to handle connection
//Handles reading from the BT2 Renogy device
PAYLOAD_STRUCTURE scrPayload;
BT2Reader bt2Reader;
SOKReader sokReader;
BTDevice *targetedDevices[] = {&bt2Reader,&sokReader};

//state
long lastScrUpdatetime=0;
int renogyCmdSequenceToSend=0;
long lastCheckedTime=0;
boolean tiktok=true;


void setup() 
{
	Serial.begin(115200);
	delay(3000);

    //Setup screen
    screen.init();
	screen.setBrightness(STND_BRIGHTNESS);

    //Init circular meters
    int cx=lcd.width()/2;
    int cy=(lcd.height()/2)+10;
    centerOutMeter.initMeter(0,20,cx,cy,90,RED2GREEN); 
    centerInMeter.initMeter(0,20,cx,cy,70,GREEN2RED); 

    //Linear meters
    //(label,vmin,vmax,scale,ticks,majorTicks,x,y,height,width);
    int lx=10; int ly=30; int width=36;
    socMeter.drawMeter("SoC", 50, 100, 100, 9, 5, lx, ly, 150, width);
    waterMeter.drawMeter("Wtr", 0, 100, 100, 5, 3, lcd.width()-width-lx, ly, 150, width);

    //Static labels
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.drawString("Sprinkle Data",80, 5, 4);

	//BLE
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

	//Setup Renogy BT2
	bt2Reader.setLoggingLevel(BT2READER_VERBOSE);

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
	Serial.printf("Found device '%s' at '%s' with uuuid '%s'\n",peripheral.localName().c_str(),peripheral.address().c_str(),peripheral.advertisedServiceUuid().c_str());

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
	Serial.printf("Connect callback for '%s'\n",peripheral.address().c_str());
	if(memcmp(peripheral.address().c_str(),bt2Reader.peripheryAddress,6)==0)
	{
		Serial.println("Renogy connect callback");
		if (bt2Reader.connectCallback(&peripheral)) 
		{
			Serial.printf("Connected to BT device %s\n",peripheral.address().c_str());
			bt2Reader.sendStartupCommand();
		}
	}

	if(memcmp(peripheral.address().c_str(),sokReader.peripheryAddress,6)==0)
	{
		Serial.println("SOK connect callback");
		if (sokReader.connectCallback(&peripheral)) 
		{
			Serial.printf("Connected to SOK device %s\n",peripheral.address().c_str());	
		}
	}	

	//Should I stop scanning now?
	boolean stopScanning=true;
	int numOfTargetedDevices=sizeof(targetedDevices)/(sizeof(targetedDevices[0]));
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
	if(memcmp(peripheral.address().c_str(),bt2Reader.peripheryAddress,6)==0)
	{	
		renogyCmdSequenceToSend=0;  //need to start at the beginning since we disconnected
		bt2Reader.disconnectCallback(&peripheral);

		Serial.printf("Disconnected BT2 device %s",peripheral.address().c_str());	
	}

	if(memcmp(peripheral.address().c_str(),sokReader.peripheryAddress,6)==0)
	{	
		Serial.printf("Disconnected SOK device %s",peripheral.address().c_str());
	}	

	Serial.println(" - Starting scan again");
	BLE.scan();
}

void mainNotifyCallback(BLEDevice peripheral, BLECharacteristic characteristic)
{
	Serial.printf("Characteristic notify from %s\n",peripheral.address().c_str());
	if(memcmp(peripheral.address().c_str(),bt2Reader.peripheryAddress,6)==0)
	{		
		bt2Reader.notifyCallback(&peripheral,&characteristic);
	}

	if(memcmp(peripheral.address().c_str(),sokReader.peripheryAddress,6)==0)
	{		
		sokReader.notifyCallback(&peripheral,&characteristic); 
	}	
}

void loop() 
{
	//required for the ArduinoBLE library
	BLE.poll();

	//load values
	if(millis()>lastScrUpdatetime+SCR_UPDATE_TIME)
	{
		lastScrUpdatetime=millis();
		//loadSimulatedValues();
		loadValues();
		updateLCD();
	}

	//Time to ask for data again?
	if (tiktok && bt2Reader.connected && millis()>lastCheckedTime+POLL_TIME_MS) 
	{
		bt2Reader.lastCheckedTime=millis();
		lastCheckedTime=millis();
		bt2Reader.sendSolarOrAlternaterCommand();
	}

	if (!tiktok && sokReader.connected && millis()>lastCheckedTime+POLL_TIME_MS) 
	{
		sokReader.lastCheckedTime=millis();
		lastCheckedTime=millis();
		sokReader.sendReadCommand();
	}

	//Do we have any data waiting?
	if(bt2Reader.getIsNewDataAvailable())
	{
		bt2Reader.updateValues();
	}
	if(sokReader.getIsNewDataAvailable())
	{
		sokReader.updateValues();
	}

	//toggle to the other device
	tiktok=!tiktok;
}

void updateLCD()
{
	//update center meter
    centerOutMeter.drawMeter(scrPayload.chargeAh);
    centerInMeter.drawMeter(scrPayload.drawAh);
    centerInMeter.drawText("Ah",scrPayload.chargeAh-scrPayload.drawAh);

    //update linear meters
    socMeter.updatePointer(scrPayload.stateOfCharge,2,0);
    waterMeter.updatePointer(scrPayload.stateOfWater,2,0);

    //update text values
    volts.drawText(125,200,scrPayload.volts,1,"V",4);
    battHoursLeft.drawText(10,190,scrPayload.batteryHoursRem,1," Hrs",2);
    waterDaysLeft.drawRightText(lcd.width()-10,190,scrPayload.waterDaysRem,1," Dys",2);
    //hertz.drawRightText(lcd.width()-10,220,hz,0,"Hz",2);
}

void loadValues()
{
	scrPayload.stateOfCharge=sokReader.getSoc();
	scrPayload.stateOfWater=random(3);
	if(scrPayload.stateOfWater==0)  scrPayload.stateOfWater=20;
	if(scrPayload.stateOfWater==1)  scrPayload.stateOfWater=50;
	if(scrPayload.stateOfWater==2)  scrPayload.stateOfWater=90;
	scrPayload.volts=sokReader.getVolts();
	scrPayload.chargeAh=bt2Reader.getSolarAmps()+bt2Reader.getAlternaterAmps();
	scrPayload.drawAh=sokReader.getAmps()-scrPayload.chargeAh;
	scrPayload.batteryHoursRem=((double)random(99))/10.0;   // 0 --> 99
	scrPayload.waterDaysRem=((double)random(10))/10.0;   // 0 --> 10
}

void loadSimulatedValues()
{
  scrPayload.stateOfCharge=random(50)+50;  // 50-->100
  scrPayload.stateOfWater=random(3);
  if(scrPayload.stateOfWater==0)  scrPayload.stateOfWater=20;
  if(scrPayload.stateOfWater==1)  scrPayload.stateOfWater=50;
  if(scrPayload.stateOfWater==2)  scrPayload.stateOfWater=90;
  scrPayload.volts=((double)random(40)+100.0)/10.0;  // 10.0 --> 14.0
  scrPayload.chargeAh=((double)random(200))/10.0;  // 0 --> 20.0
  scrPayload.drawAh=((double)random(200))/10.0;  // 0 --> 20.0
  scrPayload.batteryHoursRem=((double)random(99))/10.0;   // 0 --> 99
  scrPayload.waterDaysRem=((double)random(10))/10.0;   // 0 --> 10
}