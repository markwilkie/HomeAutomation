#include <ESP32Time.h>  // https://github.com/fbiego/ESP32Time/blob/main/examples/esp32_time/esp32_time.ino

#include "src/ble/ble.h"
#include "src/display/display.h"
#include "src/wifi/wifi.h"
#include "src/logging/logging.h"

#include "WaterTank.h"

#define TIMEAPI_URL "http://worldtimeapi.org/api/timezone/America/Los_Angeles"

#define POLL_TIME_MS	500
#define SCR_UPDATE_TIME 500
#define BITMAP_UPDATE_TIME 5000
#define BT_TIMEOUT_MS	5000
#define REFRESH_RTC (30L*60L*1000L)    //every 30 minutes

//Objects to handle connection
//Handles reading from the BT2 Renogy device
BT2Reader bt2Reader;
SOKReader sokReader;
BTDevice *targetedDevices[] = {&bt2Reader,&sokReader};

Screen screen;
VanWifi wifi;
ESP32Time rtc(0);
Layout layout;
Logger logger;
PowerLogger powerLogger;

WaterTank waterTank;

//state
BLE_SEMAPHORE bleSemaphore;
long lastRTCUpdateTime=0;
long lastScrUpdatetime=0;
long lastBitmapUpdatetime=0;
long lastPwrUpdateTime=0;
int renogyCmdSequenceToSend=0;
long lastCheckedTime=0;
long hertzTime=0;
int hertzCount=0;
boolean tiktok=true;

void setup() 
{
	#ifdef SERIALLOGGER
		//Init Serial
		Serial.begin(115200);
	#endif
	delay(3000);

	//init screen and draw initial form
	logger.log("Drawing form");
	layout.init();
	layout.drawInitialScreen();

	logger.log("Starting wifi...");
    wifi.startWifi();

	//Reset power logger
	powerLogger.reset(&rtc);

	//Init water tank
	waterTank.init();

	//Start BLE
	startBLE();

	//Set BLE callback functions
	BLE.setEventHandler(BLEDiscovered, scanCallback);
	BLE.setEventHandler(BLEConnected, connectCallback);
	BLE.setEventHandler(BLEDisconnected, disconnectCallback);	

	//Set touch callback functions
	screen.addTouchCallback(&screenTouchedCallback);
	screen.addLongTouchCallback(&longScreenTouchedCallback);

	// start scanning for peripherals
	delay(2000);
	logger.log("Starting BLE scan");	
	BLE.scan(true);  //scan with duplicates - meaning, it'll keep showing the same ones over and over

	//setting time
	setTime();	

	logger.sendLogs(wifi.isConnected());
}

void startBLE()
{
	logger.log("ArduinoBLE (via ESP32-S3) connecting to Renogy BT-2 and SOK battery");
	if (!BLE.begin()) 
	{
		logger.log(ERROR,"Starting Bluetooth® Low Energy module failed!.  Restarting in 10 seconds.");
		delay(10000);
		ESP.restart();
	}
}

void setTime()
{
	if(!wifi.isConnected())
	{
		wifi.startWifi();
		if(!wifi.isConnected())
		{
			logger.log("Tried to get latest time, but no internet connection");
			return;
		}
	}

	logger.log("Getting latest time...");
    DynamicJsonDocument timeDoc=wifi.sendGetMessage(TIMEAPI_URL);	
	long epoch=timeDoc["unixtime"].as<long>();
    long offset=timeDoc["raw_offset"].as<long>();
    long dstOffset=timeDoc["dst_offset"].as<long>();

	rtc.offset=offset+dstOffset;		
	rtc.setTime(epoch);
	lastRTCUpdateTime=millis();
}

/** Scan callback method.  
 */
void scanCallback(BLEDevice peripheral) 
{
    // discovered a peripheral, print out address, local name, and advertised service
	//logger.log("Found device '%s' at '%s' with uuuid '%s'",peripheral.localName().c_str(),peripheral.address().c_str(),peripheral.advertisedServiceUuid().c_str());

	//Checking with both device handlers to see if this is something they're interested in
	bt2Reader.scanCallback(&peripheral,&bleSemaphore);
	sokReader.scanCallback(&peripheral,&bleSemaphore);
}

/** Connect callback method.  Exact order of operations is up to the user.  if bt2Reader attempted a connection
 * it returns true (whether or not it succeeds).  Usually you can then skip any other code in this connectCallback
 * method, because it's unlikely to be relevant for other possible connections, saving CPU cycles
 */ 
void connectCallback(BLEDevice peripheral) 
{
	logger.log(INFO,"Connect callback for '%s'",peripheral.address().c_str());
	if(memcmp(peripheral.address().c_str(),bt2Reader.getPeripheryAddress(),6)==0)
	{
		logger.log(INFO,"Renogy connect callback");
		if (bt2Reader.connectCallback(&peripheral,&bleSemaphore)) 
		{
			logger.log(INFO,"Connected to BT device %s",peripheral.address().c_str());
			bt2Reader.sendStartupCommand(&bleSemaphore);
		}
	}

	if(memcmp(peripheral.address().c_str(),sokReader.getPeripheryAddress(),6)==0)
	{
		logger.log("SOK connect callback");
		if (sokReader.connectCallback(&peripheral,&bleSemaphore)) 
		{
			logger.log(INFO,"Connected to SOK device %s",peripheral.address().c_str());	
		}
	}

	//We're at least partially connected
	layout.setBLEIndicator(TFT_YELLOW);

	//Should I stop scanning now?
	boolean stopScanning=true;
	int numOfTargetedDevices=sizeof(targetedDevices)/(sizeof(targetedDevices[0]));
	for(int i=0;i<numOfTargetedDevices;i++)
	{
		if(!targetedDevices[i]->isConnected())
			stopScanning=false;
	}
	if(stopScanning)
	{
		layout.setBLEIndicator(TFT_BLUE);
		logger.log("All devices connected.  Showing online and stopping scan");
		BLE.stopScan();
	}
}

void disconnectCallback(BLEDevice peripheral) 
{
	bleSemaphore.waitingForConnection=false;
	bleSemaphore.waitingForResponse=false;

	if(memcmp(peripheral.address().c_str(),bt2Reader.getPeripheryAddress(),6)==0)
	{	
		renogyCmdSequenceToSend=0;  //need to start at the beginning since we disconnected
		bt2Reader.disconnectCallback(&peripheral);

		logger.log(WARNING,"Disconnected BT2 device %s",peripheral.address().c_str());	
	}

	if(memcmp(peripheral.address().c_str(),sokReader.getPeripheryAddress(),6)==0)
	{	
		logger.log(WARNING,"Disconnected SOK device %s",peripheral.address().c_str());
	}	

	layout.setBLEIndicator(TFT_DARKGRAY);
	logger.log(INFO,"Starting BLE scan again");
	BLE.scan();
}

void mainNotifyCallback(BLEDevice peripheral, BLECharacteristic characteristic)
{
	//logger.log("Characteristic notify from %s",peripheral.address().c_str());
	if(memcmp(peripheral.address().c_str(),bt2Reader.getPeripheryAddress(),6)==0)
	{		
		bt2Reader.notifyCallback(&peripheral,&characteristic,&bleSemaphore);
	}

	if(memcmp(peripheral.address().c_str(),sokReader.getPeripheryAddress(),6)==0)
	{		
		sokReader.notifyCallback(&peripheral,&characteristic,&bleSemaphore); 
	}	
}

void screenTouchedCallback(int x,int y)
{
	//nothing special for short touch
}

void turnOffBLE()
{
	layout.setBLEIndicator(TFT_BLACK);			
	BLE.stopScan();
	BLE.disconnect();
	//BLE.end();  //crashes the ESP32-S3  https://github.com/arduino-libraries/ArduinoBLE/issues/192??
}

void turnOnBLE()
{
	layout.setBLEIndicator(TFT_DARKGRAY);
	startBLE();
	logger.log(INFO,"Starting BLE scan again");
	BLE.scan();	
}


void longScreenTouchedCallback(int x,int y)
{
	//check if in the BLE region - meaning, should we toggle BLE on/off
	if(layout.isBLERegion(x,y))
	{
		static bool BLEOn=true;
		BLEOn=!BLEOn;
		logger.log(INFO,"BLE Toggled %d (%d,%d)",BLEOn,x,y);
		if(!BLEOn)
		{
			turnOffBLE();
		}
		else
		{
			turnOnBLE();
		}
	}
}

void loop() 
{
	//required for the ArduinoBLE library
	BLE.poll();
	screen.poll();

	//check for BLE timeouts - and disconnect
	if(isTimedout())
	{
		if(bleSemaphore.btDevice->getBLEDevice())
			bleSemaphore.btDevice->getBLEDevice()->disconnect();
		bleSemaphore.waitingForConnection=false;
		bleSemaphore.waitingForResponse=false;
	}

	//time to update power logs?
	if(millis()>lastPwrUpdateTime+PWR_UPD_TIME)
	{
		lastPwrUpdateTime=millis();

		//Time to update day spark?
		powerLogger.data.dayAhSum+=layout.displayData.chargeAmps-layout.displayData.drawAmps;
		if(rtc.getEpoch()>=(powerLogger.data.startDaySeconds+DAY_AH_INT))
		{
			float avgDayAh=powerLogger.data.dayAhSum/DAY_AH_INT;
			float dayVal=avgDayAh/(3600.0/NIGHT_AH_INT);    //convert to amp hours
			layout.addToDayAhSpark(dayVal);
			powerLogger.resetDay(&rtc);
		}

		//night time?
		if(rtc.getHour(true)>=NIGHT_BEG_HR || rtc.getHour(true)<NIGHT_END_HR)
		{
			//reset spark because it's the beginning of the evening
			if(rtc.getHour(true)==NIGHT_BEG_HR && (rtc.getEpoch()-powerLogger.data.startNightSeconds) > NIGHT_AH_DUR)
			{
				logger.log("Resetting night spark");
				layout.resetNightAhSpark();
			}

			//Time to update night spark?
			powerLogger.data.nightAhSum+=layout.displayData.chargeAmps-layout.displayData.drawAmps;
			if(rtc.getEpoch()>=(powerLogger.data.startNightSeconds+NIGHT_AH_INT))
			{
				float avgNightAh=powerLogger.data.nightAhSum/NIGHT_AH_INT;
				float nightVal=avgNightAh/(3600.0/NIGHT_AH_INT);    //convert to amp hours
				layout.addToNightAhSpark(nightVal);
				powerLogger.resetNight(&rtc);
			}
		}
	}

	//load values
	if(millis()>lastScrUpdatetime+SCR_UPDATE_TIME)
	{
		lastScrUpdatetime=millis();
		//loadSimulatedValues();
		loadValues();
		layout.updateLCD(&rtc);
		layout.setWifiIndicator(wifi.isConnected());

		//If rtc is stale, be sure and update it from the interwebs

		//send logs
		logger.sendLogs(wifi.isConnected());
	}

	if(millis()>lastBitmapUpdatetime+BITMAP_UPDATE_TIME && lastBitmapUpdatetime>0)
	{
		//Update updateBitmaps
		lastBitmapUpdatetime=millis();
		layout.updateBitmaps();

		//Check that we're still receiving BLE data
		if((!sokReader.isCurrent() && sokReader.isConnected()) || (!bt2Reader.isCurrent() && bt2Reader.isConnected()))
		{
			turnOffBLE();
			delay(1000);
			turnOnBLE();
		}
	}

	//Time to ask for data again?
	if (tiktok && bt2Reader.isConnected() && millis()>lastCheckedTime+POLL_TIME_MS) 
	{
		lastCheckedTime=millis();
		bt2Reader.sendSolarOrAlternaterCommand(&bleSemaphore);
	}

	if (!tiktok && sokReader.isConnected() && millis()>lastCheckedTime+POLL_TIME_MS) 
	{
		lastCheckedTime=millis();
		sokReader.sendReadCommand(&bleSemaphore);
	}

	//Do we have any data waiting?
	if(bt2Reader.getIsNewDataAvailable())
	{
		bt2Reader.updateValues();
	}
	if(sokReader.getIsNewDataAvailable())
	{
		hertzCount++;
		sokReader.updateValues();
	}

	//Time to refresh rtc?
	if((lastRTCUpdateTime+REFRESH_RTC) < millis())
	{
		lastRTCUpdateTime=millis();
		setTime();
	}

	//toggle to the other device
	tiktok=!tiktok;

	//calc hertz
	if(millis()>hertzTime+1000)
	{
		layout.displayData.currentHertz=hertzCount;
		hertzTime=millis();
		hertzCount=0;
	}
}

boolean isTimedout()
{
	boolean timedOutFlag=false;

	if((bleSemaphore.startTime+BT_TIMEOUT_MS) > millis()  && (bleSemaphore.waitingForConnection || bleSemaphore.waitingForResponse))
	{
		timedOutFlag=true;
		if(bleSemaphore.waitingForConnection)
		{
			logger.log("ERROR: Timed out waiting for connection to %s",bleSemaphore.btDevice->getPerifpheryName());
		}
		else if(bleSemaphore.waitingForResponse)
		{
			logger.log("ERROR: Timed out waiting for response from %s",bleSemaphore.btDevice->getPerifpheryName());
		}
		else
		{
			logger.log("ERROR: Timed out for an unknown reason");
		}
	}

	return timedOutFlag;
}

void loadValues()
{
	layout.displayData.stateOfCharge=sokReader.getSoc();
	layout.displayData.currentVolts=sokReader.getVolts();
	layout.displayData.chargeAmps=bt2Reader.getSolarAmps()+bt2Reader.getAlternaterAmps();
	layout.displayData.drawAmps=layout.displayData.chargeAmps-sokReader.getAmps();
	if(sokReader.getAmps()<0)
		layout.displayData.batteryHoursRem=sokReader.getCapacity()/(sokReader.getAmps()*-1);
	else
		layout.displayData.batteryHoursRem=999;

	int waterReading=waterTank.readLevel();
	if(waterReading==0)  { layout.displayData.stateOfWater=0; layout.displayData.rangeForWater=20; }
	if(waterReading==20)  { layout.displayData.stateOfWater=20; layout.displayData.rangeForWater=50; }
	if(waterReading==50)  { layout.displayData.stateOfWater=50; layout.displayData.rangeForWater=90; }
	if(waterReading==90)  { layout.displayData.stateOfWater=90; layout.displayData.rangeForWater=100; }
	//layout.displayData.waterDaysRem=((double)random(10))/10.0;   // 0 --> 10

	layout.displayData.batteryTemperature=sokReader.getTemperature();
	layout.displayData.chargerTemperature=bt2Reader.getTemperature();
	layout.displayData.batteryAmpValue=sokReader.getAmps();
	layout.displayData.solarAmpValue=bt2Reader.getSolarAmps();
	layout.displayData.alternaterAmpValue=bt2Reader.getAlternaterAmps();

	layout.displayData.heater=sokReader.isHeating();

	layout.displayData.cmos=sokReader.isCMOS();
	layout.displayData.dmos=sokReader.isDMOS();
}

void loadSimulatedValues()
{
  layout.displayData.stateOfCharge=random(90)+10;  // 10-->100
  layout.displayData.stateOfWater=random(4);
  if(layout.displayData.stateOfWater==0)  { layout.displayData.stateOfWater=0; layout.displayData.rangeForWater=20; }
  if(layout.displayData.stateOfWater==1)  { layout.displayData.stateOfWater=20; layout.displayData.rangeForWater=50; }
  if(layout.displayData.stateOfWater==2)  { layout.displayData.stateOfWater=50; layout.displayData.rangeForWater=90; }
  if(layout.displayData.stateOfWater==3)  { layout.displayData.stateOfWater=90; layout.displayData.rangeForWater=100; }

  layout.displayData.chargeAmps=((double)random(150))/9.2;  // 0 --> 15.0
  layout.displayData.drawAmps=((double)random(100))/9.2;  // 0 --> 10.0

  layout.displayData.currentVolts=((double)random(40)+100.0)/10.0;  // 10.0 --> 14.0
  layout.displayData.batteryHoursRem=((double)random(99))/10.0;   // 0 --> 99
  layout.displayData.waterDaysRem=((double)random(10))/10.0;   // 0 --> 10

  layout.displayData.batteryTemperature=((double)random(90))-20;  // 20 --> 70
  layout.displayData.chargerTemperature=((double)random(90))-20;  // 20 --> 70
  layout.displayData.batteryAmpValue=((double)random(200))/10.0;  // 0 --> 20.0
  layout.displayData.solarAmpValue=((double)random(200))/10.0;  // 0 --> 20.0
  layout.displayData.alternaterAmpValue=((double)random(200))/10.0;  // 0 --> 20.0 

  layout.displayData.heater=random(2);

  layout.displayData.cmos=random(2);
  layout.displayData.dmos=random(2);
}