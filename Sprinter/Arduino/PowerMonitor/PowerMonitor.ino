#include <ESP32Time.h>  // https://github.com/fbiego/ESP32Time/blob/main/examples/esp32_time/esp32_time.ino

#include "src/ble/ble.h"
#include "src/display/display.h"
#include "src/wifi/wifi.h"
#include "src/logging/logging.h"

#include "WaterTank.h"
#include "GasTank.h"

#define TIMEAPI_URL "https://world-time-api3.p.rapidapi.com/timezone/America/Los_Angeles"
//#define TIMEAPI_URL "https://timeapi.io/api/time/current/zone?timeZone=America/Los_Angeles"
//#define TIMEAPI_URL "http://worldtimeapi.org/api/timezone/America/Los_Angeles"

#define POLL_TIME_MS	500
#define TANK_CHECK_TIME 300000       // Check gas and water tank every 5 minutes
#define WIFI_CHECK_TIME 60000       // Check WiFi connection every 1 minute
#define SOC_LOG_TIME 60000          // Log SOC from each battery every 1 minute
#define REFRESH_RTC (30L*60L*1000L)    //every 30 minutes

//Objects to handle connection
//Handles reading from the BT2 Renogy device
BT2Reader bt2Reader;
SOKReader sokReader1("SOK-AA12487",1);  // First SOK battery (default name: SOK-AA12487)
SOKReader sokReader2("SOK-AA53284",2);  // Second SOK battery (customize name as needed)

// BLE Manager handles all BLE operations
BLEManager bleManager;

// Screen Controller handles display and touch
ScreenController screenController;

Screen screen;
VanWifi wifi;
ESP32Time rtc(0); 
Layout layout;

Logger logger;
PowerLogger powerLogger;

WaterTank waterTank;
GasTank gasTank;

long lastRTCUpdateTime=0;
long lastPwrUpdateTime=0;
long lastTankCheckTime=0;
long lastWifiCheckTime=0;
long lastSocLogTime=0;
long lastCheckedTime=0;

// BLE indicator callback for UI
void bleIndicatorCallback(uint32_t color) {
	layout.setBLEIndicator(color);
}

void setup() 
{
	//#ifdef SERIALLOGGER
		//Init Serial
		Serial.begin(115200);
		while(!Serial && millis() < 5000) { } // Wait up to 5 seconds for USB CDC to connect
	//#endif
	delay(1000);

	Serial.println("=== PowerMonitor Starting ===");  // Direct serial test

	//init screen and draw initial form
	logger.log(WARNING,"Booting...");
	layout.init();
	layout.displayData.batteryMode = BATTERY_COMBINED;  // Initialize to combined view
	layout.drawInitialScreen();

    wifi.startWifi();

	//Reset power logger
	powerLogger.reset(&rtc);

	//Start BLE Manager
	bleManager.init();
	bleManager.registerDevice(&bt2Reader);
	bleManager.registerDevice(&sokReader1);
	bleManager.registerDevice(&sokReader2);
	bleManager.setIndicatorCallback(bleIndicatorCallback);

	//Initialize Screen Controller
	screenController.init(&layout, &screen, &bleManager);
	screenController.setDeviceReaders(&bt2Reader, &sokReader1, &sokReader2);
	screenController.setTankReaders(&waterTank, &gasTank);

	//setting time BEFORE starting scan (avoids scan callback during HTTP request)
	setTime();	

	//Read tanks initially
	waterTank.readWaterLevel();
	waterTank.updateDaysRemaining();

	gasTank.readGasLevel();
	gasTank.updateDaysRemaining();	

	// start scanning for peripherals
	delay(1000);
	logger.log(WARNING,"Starting BLE scan for the first time");	
	bleManager.startScanning();

	logger.sendLogs(wifi.isConnected());
}

void setTime()
{
	if(!wifi.isConnected())
	{
		wifi.startWifi();
		if(!wifi.isConnected())
		{
			logger.log(WARNING,"Tried to get latest time, but no internet connection");
			return;
		}
	}

	logger.log(INFO,"Getting latest time...");
    DynamicJsonDocument timeDoc=wifi.sendGetMessage(TIMEAPI_URL);	
	
	// Debug: log the raw response
	String debugOutput;
	serializeJson(timeDoc, debugOutput);
	//logger.log(INFO, "Time API response: %s", debugOutput.c_str());
	
	long epoch=timeDoc["unixtime"].as<long>();
    long offset=timeDoc["raw_offset"].as<long>();
    long dstOffset=timeDoc["dst_offset"].as<long>();

	rtc.offset=offset+dstOffset;		
	rtc.setTime(epoch);

	/*
	int seconds=timeDoc["seconds"].as<int>();
	int minutes=timeDoc["minute"].as<int>();
	int hours=timeDoc["hour"].as<int>();
	int day=timeDoc["day"].as<int>();
	int month=timeDoc["month"].as<int>();
	int year=timeDoc["year"].as<int>();

	// Sanity check the values
	if(year < 2020 || month < 1 || month > 12 || day < 1 || day > 31)
	{
		logger.log(ERROR, "Invalid time values received: %d/%d/%d - resetting to epoch", month, day, year);
		rtc.setTime(0);  // Reset to epoch (1/1/1970)
		return;
	}

	rtc.setTime(seconds, minutes, hours, day, month, year);
	logger.log(INFO,"Time set to %d/%d/%d %d:%d:%d",month,day,year,hours,minutes,seconds);
	*/

	lastRTCUpdateTime=millis();
}

void loop() 
{
	screen.poll();
	
	// BLE Manager handles connections, timeouts, and device monitoring
	bleManager.poll();

	// If waiting for BLE connection or response, only poll screen and yield
	if(bleManager.isWaiting())
	{
		delay(1);  // Yield to BLE stack
		return;    // Return early - don't process other tasks while waiting
	}

	//Do we have any data waiting?
	if(bt2Reader.getIsNewDataAvailable())
	{
		screenController.incrementHertzCount();
		bt2Reader.updateValues();
	}
	if(sokReader1.getIsNewDataAvailable())
	{
		screenController.incrementHertzCount();
		sokReader1.updateValues();
	}
	if(sokReader2.getIsNewDataAvailable())
	{
		screenController.incrementHertzCount();
		sokReader2.updateValues();
	}	

	// Check WiFi connection every 60 seconds and reconnect if needed
	if(millis() - lastWifiCheckTime > WIFI_CHECK_TIME) {
		if(!wifi.isConnected()) {
			logger.log(WARNING, "WiFi is currently disconnected, attempting reconnection...");
			wifi.startWifi();
		}
		lastWifiCheckTime = millis();
	}

	// Log SOC from each battery every 1 minute
	if(millis() - lastSocLogTime > SOC_LOG_TIME) {
		logger.log(INFO, "SOC: SOK1=%d/%fA (curr=%d, conn=%d), SOK2=%d/%fA (curr=%d, conn=%d), BT2=%fV/%fA/%fA (curr=%d, conn=%d)", 
			sokReader1.getSoc(), sokReader1.getAmps(), sokReader1.isCurrent(), sokReader1.isConnected(),
			sokReader2.getSoc(), sokReader2.getAmps(), sokReader2.isCurrent(), sokReader2.isConnected(),
			bt2Reader.getTemperature(), bt2Reader.getSolarAmps(), bt2Reader.getAlternaterAmps(), bt2Reader.isCurrent(), bt2Reader.isConnected());
		lastSocLogTime = millis();
	}

	// Screen controller handles screen updates, touch callbacks, and loading values
	screenController.update(&rtc, wifi.isConnected());

	//time to update power logs?
	if(millis()>lastPwrUpdateTime+PWR_UPD_TIME)
	{
		lastPwrUpdateTime=millis();
		powerLogger.add(layout.displayData.chargeAmps-layout.displayData.drawAmps,&rtc,&layout);
		
		delay(50); // Delay after screen refresh to avoid power spike 
	}	

	// Check gas and water tank every 10 minutes
	// Note: We already returned early above if waiting for BLE connection/response
	if(millis() - lastTankCheckTime > TANK_CHECK_TIME) 
	{
		// Stop BLE first to avoid timeouts during blocking operations
		bleManager.turnOff();
		
		if(wifi.isConnected()) {
			wifi.stopWifi();
			delay(100);  // Brief delay for WiFi to fully stop
		}
		waterTank.readWaterLevel();
		waterTank.updateDaysRemaining();

		gasTank.readGasLevel();
		gasTank.updateDaysRemaining();		

		wifi.startWifi();
		delay(100);  // Brief delay for WiFi to start
		
		// Restart BLE after WiFi is back up
		bleManager.turnOn();
		
		lastTankCheckTime = millis();
	}

	//Time to refresh rtc?
	// Note: We already returned early above if waiting for BLE connection/response
	if((lastRTCUpdateTime+REFRESH_RTC) < millis())
	{
		lastRTCUpdateTime=millis();
		setTime();
	}

	// Send BLE command at end of loop - cycle through devices
	// Note: We already returned early above if waiting for response/connection
	static int deviceCycle = 0;
	BLE_SEMAPHORE* bleSemaphore = bleManager.getSemaphore();
	if(millis() - lastCheckedTime > POLL_TIME_MS)
	{
		lastCheckedTime = millis();
		if (deviceCycle == 0 && bt2Reader.isConnected()) 
		{
			if (bt2Reader.needsStartupCommand()) {
				bt2Reader.sendStartupCommand(bleSemaphore);
			} else {
				bt2Reader.sendSolarOrAlternaterCommand(bleSemaphore);
			}
		}
		else if (deviceCycle == 1 && sokReader1.isConnected()) 
		{
			sokReader1.sendReadCommand(bleSemaphore);
		}
		else if (deviceCycle == 2 && sokReader2.isConnected()) 
		{
			sokReader2.sendReadCommand(bleSemaphore);
		}
		deviceCycle = (deviceCycle + 1) % 3;
	}
	else
	{
		logger.sendLogs(wifi.isConnected());	
		delay(50); // Delay to avoid power spike
	}

	// CRITICAL: Yield to allow NimBLE host task to process BLE events (notifications, etc.)
	// Without this, the busy loop can starve the BLE task and notifications won't be delivered.
	delay(1);
}
