#include <ESP32Time.h>  // https://github.com/fbiego/ESP32Time/blob/main/examples/esp32_time/esp32_time.ino

#include "src/ble/ble.h"
#include "src/display/display.h"
#include "src/wifi/wifi.h"
#include "src/logging/logging.h"

#include "WaterTank.h"
#include "GasTank.h"

#define TIMEAPI_URL "https://timeapi.io/api/time/current/zone?timeZone=America/Los_Angeles"
//#define TIMEAPI_URL "http://worldtimeapi.org/api/timezone/America/Los_Angeles"

#define POLL_TIME_MS	500
#define SCR_UPDATE_TIME 500
#define BITMAP_UPDATE_TIME 5000
#define BLE_IS_ALIVE_TIME 30000
#define BT_TIMEOUT_MS	5000
#define WATER_CHECK_TIME 60000      // Check water tank every 1 minute
#define GAS_CHECK_TIME 600000       // Check gas tank every 10 minutes
#define WIFI_CHECK_TIME 60000       // Check WiFi connection every 1 minute
#define REFRESH_RTC (30L*60L*1000L)    //every 30 minutes

//Objects to handle connection
//Handles reading from the BT2 Renogy device
BT2Reader bt2Reader;
SOKReader sokReader1("SOK-AA12487",1);  // First SOK battery (default name: SOK-AA12487)
SOKReader sokReader2("SOK-AA53284",2);  // Second SOK battery (customize name as needed)
BTDevice *targetedDevices[] = {&bt2Reader,&sokReader1,&sokReader2};

Screen screen;
VanWifi wifi;
ESP32Time rtc(0); 
Layout layout;

// Screen state management
enum ScreenState {
	MAIN_SCREEN,
	WATER_DETAIL_SCREEN
};
ScreenState currentScreen = MAIN_SCREEN;
Logger logger;
PowerLogger powerLogger;

WaterTank waterTank;
WaterPump waterPump;
GasTank gasTank;

//state
BLE_SEMAPHORE bleSemaphore;
long lastRTCUpdateTime=0;
long lastScrUpdatetime=0;
long lastBitmapUpdatetime=0;
long lastBleIsAliveTime=0;
long lastPwrUpdateTime=0;
long lastWaterCheckTime=0;
long lastGasCheckTime=0;
long lastWifiCheckTime=0;
int renogyCmdSequenceToSend=0;
long lastCheckedTime=0;
long hertzTime=0;
int hertzCount=0;

void setup() 
{
	//#ifdef SERIALLOGGER
		//Init Serial
		Serial.begin(115200);
	//#endif
	delay(3000);

	//init screen and draw initial form
	logger.log("Drawing form");
	layout.init();
	layout.displayData.batteryMode = BATTERY_COMBINED;  // Initialize to combined view
	layout.drawInitialScreen();

	logger.log("Starting wifi...");
    wifi.startWifi();

	//Reset power logger
	powerLogger.reset(&rtc);

	//Init water tank and pump
	//waterTank.init();
	waterPump.init();

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

	//set time to current so it waits before checking the first time
	lastBleIsAliveTime=millis();
	lastBitmapUpdatetime=millis();

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
	
	/*
	long epoch=timeDoc["unixtime"].as<long>();
    long offset=timeDoc["raw_offset"].as<long>();
    long dstOffset=timeDoc["dst_offset"].as<long>();

	rtc.offset=offset+dstOffset;		
	rtc.setTime(epoch);
	*/

	int seconds=timeDoc["seconds"].as<int>();
	int minutes=timeDoc["minute"].as<int>();
	int hours=timeDoc["hour"].as<int>();
	int day=timeDoc["day"].as<int>();
	int month=timeDoc["month"].as<int>();
	int year=timeDoc["year"].as<int>();

	rtc.setTime(seconds, minutes, hours, day, month, year);
	logger.log(INFO,"Time set to %d/%d/%d %d:%d:%d",month,day,year,hours,minutes,seconds);

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
	sokReader1.scanCallback(&peripheral,&bleSemaphore);
	sokReader2.scanCallback(&peripheral,&bleSemaphore);
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

	if(memcmp(peripheral.address().c_str(),sokReader1.getPeripheryAddress(),6)==0)
	{
		logger.log("SOK Battery 1 connect callback");
		if (sokReader1.connectCallback(&peripheral,&bleSemaphore)) 
		{
			logger.log(INFO,"Connected to SOK Battery 1 %s",peripheral.address().c_str());	
		}
	}

	if(memcmp(peripheral.address().c_str(),sokReader2.getPeripheryAddress(),6)==0)
	{
		logger.log("SOK Battery 2 connect callback");
		if (sokReader2.connectCallback(&peripheral,&bleSemaphore)) 
		{
			logger.log(INFO,"Connected to SOK Battery 2 %s",peripheral.address().c_str());	
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

	if(memcmp(peripheral.address().c_str(),sokReader1.getPeripheryAddress(),6)==0)
	{	
		logger.log(WARNING,"Disconnected SOK Battery 1 %s",peripheral.address().c_str());
	}

	if(memcmp(peripheral.address().c_str(),sokReader2.getPeripheryAddress(),6)==0)
	{	
		logger.log(WARNING,"Disconnected SOK Battery 2 %s",peripheral.address().c_str());
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

	if(memcmp(peripheral.address().c_str(),sokReader1.getPeripheryAddress(),6)==0)
	{		
		sokReader1.notifyCallback(&peripheral,&characteristic,&bleSemaphore); 
	}

	if(memcmp(peripheral.address().c_str(),sokReader2.getPeripheryAddress(),6)==0)
	{		
		sokReader2.notifyCallback(&peripheral,&characteristic,&bleSemaphore); 
	}	
}

void screenTouchedCallback(int x,int y)
{
	// Check if water region touched - toggle between main and detail screens
	if(currentScreen == MAIN_SCREEN && layout.isWaterRegion(x,y))
	{
		// Switch to water detail screen
		currentScreen = WATER_DETAIL_SCREEN;
		layout.showWaterDetail();
		logger.log(INFO,"Water detail screen shown");
	}
	else if(currentScreen == WATER_DETAIL_SCREEN)
	{
		// Return to main screen
		currentScreen = MAIN_SCREEN;
		layout.drawInitialScreen();
		logger.log(INFO,"Returned to main screen");
	}
	// Check if battery icon touched - cycle through modes
	else if(currentScreen == MAIN_SCREEN && layout.isBatteryIconRegion(x,y))
	{
		// Cycle: COMBINED -> SOK1 -> SOK2 -> COMBINED
		if(layout.displayData.batteryMode == BATTERY_COMBINED)
			layout.displayData.batteryMode = BATTERY_SOK1;
		else if(layout.displayData.batteryMode == BATTERY_SOK1)
			layout.displayData.batteryMode = BATTERY_SOK2;
		else
			layout.displayData.batteryMode = BATTERY_COMBINED;
		
		logger.log(INFO,"Battery mode: %d", layout.displayData.batteryMode);
	}
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
			disconnectBLE();
		}
		else
		{
			reStartBLE();
		}
	}
}

void disconnectBLE()
{
	layout.setBLEIndicator(TFT_BLACK);			
	BLE.stopScan();
	logger.log(INFO,"after stop scan");

	int numOfTargetedDevices=sizeof(targetedDevices)/(sizeof(targetedDevices[0]));
	for(int i=0;i<numOfTargetedDevices;i++)
	{
		logger.log(INFO,targetedDevices[i]->getPerifpheryName());
		logger.log(INFO,targetedDevices[i]->isConnected());
		//targetedDevices[i]->disconnect();
	}

	logger.log(INFO,"after disconnecting");
	
	BLE.disconnect();

	logger.log(INFO,"after ble disconnecting");

	for(int i=0;i<numOfTargetedDevices;i++)
	{
		logger.log(INFO,targetedDevices[i]->getPerifpheryName());
		logger.log(INFO,targetedDevices[i]->isConnected());
		//targetedDevices[i]->disconnect();
	}

	//BLE.end();  //crashes the ESP32-S3  https://github.com/arduino-libraries/ArduinoBLE/issues/192??	
}

void reStartBLE()
{
	layout.setBLEIndicator(TFT_DARKGRAY);
	startBLE();
	logger.log(INFO,"Starting BLE scan again");
	BLE.scan();	
}

void checkForStaleData()
{
	if((!sokReader1.isCurrent() && sokReader1.isConnected()) || (!sokReader2.isCurrent() && sokReader2.isConnected()) || (!bt2Reader.isCurrent() && bt2Reader.isConnected()))
	{
		logger.log(INFO,"BLE data is stale!  Restarting BLE");
		turnOffBLE();
		delay(1000);
		turnOnBLE();
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
		logger.log(INFO,"BLE operation timed out, disconnecting");
		if(bleSemaphore.btDevice->getBLEDevice())
			bleSemaphore.btDevice->getBLEDevice()->disconnect();
		bleSemaphore.waitingForConnection=false;
		bleSemaphore.waitingForResponse=false;
		bleSemaphore.btDevice=nullptr;
		
		// Restart scanning to give failed device another chance
		logger.log(INFO,"Restarting BLE scan after timeout");
		BLE.scan();
	}

	//time to update power logs?
	if(millis()>lastPwrUpdateTime+PWR_UPD_TIME)
	{
		lastPwrUpdateTime=millis();
		powerLogger.add(layout.displayData.chargeAmps-layout.displayData.drawAmps,&rtc,&layout);
	}

	if(millis()>lastBleIsAliveTime+BLE_IS_ALIVE_TIME)
	{
		checkForStaleData();

		//reset time
		lastBleIsAliveTime=millis();
	}

	// Check WiFi connection every 60 seconds and reconnect if needed
	if(millis() - lastWifiCheckTime > WIFI_CHECK_TIME) {
		if(!wifi.isConnected()) {
			logger.log(INFO, "WiFi is currently disconnected, attempting reconnection...");
			wifi.startWifi();
		}
		lastWifiCheckTime = millis();
	}

	//load values
	if(millis()>lastScrUpdatetime+SCR_UPDATE_TIME)
	{
		lastScrUpdatetime=millis();
		//loadSimulatedValues();
		loadValues();
		if(currentScreen == MAIN_SCREEN)
		{
			layout.updateLCD(&rtc);
		}
		layout.setWifiIndicator(wifi.isConnected());

		//water pump indicator light
		waterPump.updateLight();
		// Track water usage percentage per day
		waterTank.updateUsage();
		// Track gas usage percentage per day
		gasTank.updateUsage();
		//If rtc is stale, be sure and update it from the interwebs

		//send logs
		logger.sendLogs(wifi.isConnected());
	}

	if(millis()>lastBitmapUpdatetime+BITMAP_UPDATE_TIME)
	{
		//Update updateBitmaps
		if(currentScreen == MAIN_SCREEN)
		{
			layout.updateBitmaps();
		}

		//Check that we're still receiving BLE data
		checkForStaleData();

		//reset time
		lastBitmapUpdatetime=millis();
	}

	// Check water tank every 1 minute
	if(millis() - lastWaterCheckTime > WATER_CHECK_TIME) {
		waterTank.readWaterLevel();
		waterTank.updateDaysRemaining();
		lastWaterCheckTime = millis();
	}

	// Check gas tank every 10 minutes
	// WiFi must be disabled before reading GPIO11 due to hardware conflict
	if(millis() - lastGasCheckTime > GAS_CHECK_TIME) {
		if(wifi.isConnected()) {
			wifi.stopWifi();
			delay(1000); // Allow WiFi to fully stop
		}
		gasTank.readGasLevel();
		gasTank.updateDaysRemaining();
		wifi.startWifi();
		lastGasCheckTime = millis();
	}

	//Time to ask for data again? Cycle through devices
	static int deviceCycle = 0;
	if (millis()>lastCheckedTime+POLL_TIME_MS) 
	{
		lastCheckedTime=millis();
		if (deviceCycle == 0 && bt2Reader.isConnected()) 
		{
			bt2Reader.sendSolarOrAlternaterCommand(&bleSemaphore);
		}
		else if (deviceCycle == 1 && sokReader1.isConnected()) 
		{
			sokReader1.sendReadCommand(&bleSemaphore);
		}
		else if (deviceCycle == 2 && sokReader2.isConnected()) 
		{
			sokReader2.sendReadCommand(&bleSemaphore);
		}
		deviceCycle = (deviceCycle + 1) % 3;
	}

	//Do we have any data waiting?
	if(bt2Reader.getIsNewDataAvailable())
	{
		bt2Reader.updateValues();
	}
	if(sokReader1.getIsNewDataAvailable())
	{
		hertzCount++;
		sokReader1.updateValues();
	}
	if(sokReader2.getIsNewDataAvailable())
	{
		sokReader2.updateValues();
	}

	//Time to refresh rtc?
	if((lastRTCUpdateTime+REFRESH_RTC) < millis())
	{
		lastRTCUpdateTime=millis();
		setTime();
	}

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

	if((bleSemaphore.startTime+BT_TIMEOUT_MS) < millis()  && (bleSemaphore.waitingForConnection || bleSemaphore.waitingForResponse))
	{
		timedOutFlag=true;
		if(bleSemaphore.waitingForConnection)
		{
			logger.log(ERROR,"Timed out waiting for connection to %s",bleSemaphore.btDevice->getPerifpheryName());
		}
		else if(bleSemaphore.waitingForResponse)
		{
			logger.log(ERROR,"Timed out waiting for response from %s",bleSemaphore.btDevice->getPerifpheryName());
		}
		else
		{
			logger.log(ERROR,"Timed out for an unknown reason");
		}
	}

	return timedOutFlag;
}

void loadValues()
{
	// Individual battery data
	layout.displayData.stateOfCharge = sokReader1.getSoc();
	layout.displayData.stateOfCharge2 = sokReader2.getSoc();
	
	// Combined data from both SOK batteries
	int avgSoc = (sokReader1.getSoc() + sokReader2.getSoc()) / 2;
	float avgVolts = (sokReader1.getVolts() + sokReader2.getVolts()) / 2.0;
	float totalAmps = sokReader1.getAmps() + sokReader2.getAmps();
	float totalCapacity = sokReader1.getCapacity() + sokReader2.getCapacity();
	
	layout.displayData.currentVolts=avgVolts;
	layout.displayData.batteryVolts=sokReader1.getVolts();
	layout.displayData.batteryVolts2=sokReader2.getVolts();
	
	// Calculate charge and draw amps for display rings
	// chargeAmps: Total incoming power from solar + alternator (displayed as outer ring)
	// drawAmps: Actual house load consumption (displayed as inner ring)
	// Center number: chargeAmps - drawAmps = net battery flow (positive=charging, negative=discharging)
	layout.displayData.chargeAmps=bt2Reader.getSolarAmps()+bt2Reader.getAlternaterAmps();
	
	// Calculate actual house load based on battery state:
	// - If discharging (totalAmps negative): load = solar + battery_discharge
	//   Example: 5A solar, -10A battery → load = 5 + 10 = 15A
	// - If charging (totalAmps positive): load = solar - battery_charge
	//   Example: 15A solar, +10A battery → load = 15 - 10 = 5A
	if(totalAmps < 0)
		layout.displayData.drawAmps = layout.displayData.chargeAmps + (-totalAmps);  // Add battery discharge
	else
		layout.displayData.drawAmps = layout.displayData.chargeAmps - totalAmps;     // Subtract battery charge
	
	// Calculate hours remaining for each battery
	if(sokReader1.getAmps()<0)
		layout.displayData.batteryHoursRem=sokReader1.getCapacity()/(sokReader1.getAmps()*-1);
	else
		layout.displayData.batteryHoursRem=999;
		
	if(sokReader2.getAmps()<0)
		layout.displayData.batteryHoursRem2=sokReader2.getCapacity()/(sokReader2.getAmps()*-1);
	else
		layout.displayData.batteryHoursRem2=999;

	// Use cached water and gas tank values
	layout.displayData.stateOfWater = waterTank.getWaterLevel();
	layout.displayData.waterDaysRem = waterTank.getWaterDaysRemaining();
	layout.displayData.stateOfGas = gasTank.getGasLevel();
	layout.displayData.gasDaysRem = gasTank.getGasDaysRemaining();
	
	// Set battery-specific values based on display mode
	if(layout.displayData.batteryMode == BATTERY_COMBINED)
	{
		// Average temperature and combine amps
		layout.displayData.batteryTemperature = (sokReader1.getTemperature() + sokReader2.getTemperature()) / 2.0;
		layout.displayData.batteryAmpValue = sokReader1.getAmps() + sokReader2.getAmps();
		// AND logic for CMOS/DMOS (both must be on)
		layout.displayData.cmos = sokReader1.isCMOS() && sokReader2.isCMOS();
		layout.displayData.dmos = sokReader1.isDMOS() && sokReader2.isDMOS();
		// OR logic for heater (either heating)
		layout.displayData.heater = sokReader1.isHeating() || sokReader2.isHeating();
	}
	else if(layout.displayData.batteryMode == BATTERY_SOK1)
	{
		layout.displayData.batteryTemperature = sokReader1.getTemperature();
		layout.displayData.batteryAmpValue = sokReader1.getAmps();
		layout.displayData.cmos = sokReader1.isCMOS();
		layout.displayData.dmos = sokReader1.isDMOS();
		layout.displayData.heater = sokReader1.isHeating();
	}
	else // BATTERY_SOK2
	{
		layout.displayData.batteryTemperature = sokReader2.getTemperature();
		layout.displayData.batteryAmpValue = sokReader2.getAmps();
		layout.displayData.cmos = sokReader2.isCMOS();
		layout.displayData.dmos = sokReader2.isDMOS();
		layout.displayData.heater = sokReader2.isHeating();
	}
	
	layout.displayData.chargerTemperature=bt2Reader.getTemperature();
	layout.displayData.solarAmpValue=bt2Reader.getSolarAmps();
	layout.displayData.alternaterAmpValue=bt2Reader.getAlternaterAmps();
}

void loadSimulatedValues()
{
  // Static variables to track water percentage cycling
  static int waterPercent = 0;
  static bool waterIncreasing = true;
  
  // Update water percentage: 0 -> 100 by 10s, then back down
  if(waterIncreasing)
  {
    waterPercent += 1;
    if(waterPercent >= 100)
      waterIncreasing = false;
  }
  else
  {
    waterPercent -= 1;
    if(waterPercent <= 0)
      waterIncreasing = true;
  }
  
  layout.displayData.stateOfCharge=random(90)+10;  // 10-->100
  layout.displayData.stateOfCharge2=random(90)+10;  // 10-->100
  layout.displayData.stateOfWater=waterPercent;  // 0 -> 100 by 10s
  layout.displayData.stateOfGas=random(100);  // 0 -> 100
  
  layout.displayData.chargeAmps=((double)random(150))/9.2;  // 0 --> 15.0
  layout.displayData.drawAmps=((double)random(100))/9.2;  // 0 --> 10.0

  layout.displayData.currentVolts=((double)random(40)+100.0)/10.0;  // 10.0 --> 14.0
  layout.displayData.batteryVolts=((double)random(40)+100.0)/10.0;  // 10.0 --> 14.0
  layout.displayData.batteryVolts2=((double)random(40)+100.0)/10.0;  // 10.0 --> 14.0
  layout.displayData.batteryHoursRem=((double)random(99))/10.0;   // 0 --> 99
  layout.displayData.batteryHoursRem2=((double)random(99))/10.0;   // 0 --> 99
  layout.displayData.waterDaysRem=((double)random(10))/10.0;   // 0 --> 10
  layout.displayData.gasDaysRem=((double)random(10))/10.0;   // 0 --> 10

  layout.displayData.batteryTemperature=((double)random(90))-20;  // 20 --> 70
  layout.displayData.chargerTemperature=((double)random(90))-20;  // 20 --> 70
  layout.displayData.batteryAmpValue=((double)random(200))/10.0;  // 0 --> 20.0
  layout.displayData.solarAmpValue=((double)random(200))/10.0;  // 0 --> 20.0
  layout.displayData.alternaterAmpValue=((double)random(200))/10.0;  // 0 --> 20.0 

  layout.displayData.heater=random(2);

  layout.displayData.cmos=random(2);
  layout.displayData.dmos=random(2);
}
