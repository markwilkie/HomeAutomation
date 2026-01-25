/*
 * PowerMonitor - ESP32-S3 Based Van Power Monitoring System
 * 
 * HARDWARE:
 * - ESP32-S3 MCU (WT32-SC01 Plus development board)
 * - 480x320 TFT Touch Display (LovyanGFX driver)
 * - Analog sensors for water tank (resistive sender) and gas tank (FSR sensor)
 * 
 * BLE DEVICES MONITORED:
 * - Renogy BT2 DC-DC Charger: Solar + alternator charging data
 * - SOK Battery #1: 12V LiFePO4 battery BMS data (SOC, voltage, current, temp)
 * - SOK Battery #2: Second 12V LiFePO4 battery
 * 
 * FEATURES:
 * - Real-time power flow visualization (charge vs draw amps)
 * - Battery state of charge with color-coded status
 * - Water and gas tank levels with days-remaining estimates
 * - Night/day power consumption sparklines
 * - Touch-based UI with drill-down screens
 * - Remote logging via Papertrail
 * - WiFi time sync via RTC
 * 
 * ARCHITECTURE:
 * - BLEManager: Centralized BLE connection handling with retry/backoff
 * - ScreenController: Touch handling and screen state management
 * - Layout: Display rendering and UI components
 * - PowerLogger: Tracks power consumption over time for sparklines
 * 
 * See README.md for detailed documentation.
 */

#include <ESP32Time.h>  // https://github.com/fbiego/ESP32Time/blob/main/examples/esp32_time/esp32_time.ino

#include "src/ble/ble.h"
#include "src/display/display.h"
#include "src/wifi/wifi.h"
#include "src/logging/logging.h"

#include "src/tanks/WaterTank.h"
#include "src/tanks/GasTank.h"

#define TIMEAPI_URL "https://world-time-api3.p.rapidapi.com/timezone/America/Los_Angeles"
//#define TIMEAPI_URL "https://timeapi.io/api/time/current/zone?timeZone=America/Los_Angeles"
//#define TIMEAPI_URL "http://worldtimeapi.org/api/timezone/America/Los_Angeles"

// ============================================================================
// TIMING CONSTANTS - Adjust these to change system behavior
// ============================================================================
#define POLL_TIME_MS	500             // BLE command polling interval (how often we send commands to devices)
#define TANK_CHECK_TIME 900000       // Check gas and water tank every 15 minutes (WiFi must be off during ADC read)
#define WIFI_CHECK_TIME 60000        // Check/reconnect WiFi every 1 minute
#define SOC_LOG_TIME 300000           // Log SOC from each battery every 5 minutes (for remote debugging)
#define REFRESH_RTC (60L*60L*1000L)  // Sync RTC with internet time every hour

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================

// BLE Device Readers - Each handles protocol parsing for a specific device type
BT2Reader bt2Reader;                    // Renogy BT2 DC-DC charger (solar + alternator)
SOKReader sokReader1("SOK-AA12487",1);  // First SOK battery (BLE name from battery label)
SOKReader sokReader2("SOK-AA53284",2);  // Second SOK battery (BLE name from battery label)

// BLE Manager - Centralized connection handling with retry/backoff system
// See BLEManager.h for detailed architecture documentation
BLEManager bleManager;

// Screen Controller - Manages screen state, touch events, and display updates
ScreenController screenController;

// Core display and system objects
Screen screen;           // Low-level screen/touch driver (LovyanGFX wrapper)
VanWifi wifi;            // WiFi connection manager (multi-AP support)
ESP32Time rtc(0);        // Real-time clock (synced from internet)
Layout layout;           // Display layout and rendering

// Logging and data tracking
Logger logger;           // Remote logging via Papertrail + serial
PowerLogger powerLogger; // Tracks power consumption for sparklines

// Tank sensors
WaterTank waterTank;     // Water tank level sensor (resistive sender)
GasTank gasTank;         // Gas tank level sensor (FSR pressure sensor)

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

/*
 * setup() - One-time initialization on power-up or reset
 * 
 * INITIALIZATION ORDER IS CRITICAL:
 * 1. Serial - For debugging during boot
 * 2. Display - Show "Booting..." so user knows it's alive
 * 3. WiFi - Need network for time sync
 * 4. PowerLogger - Reset sparkline data
 * 5. BLE Manager - Register all devices before scanning
 * 6. Screen Controller - Wire up touch callbacks
 * 7. Time sync - BEFORE BLE scan starts (HTTP conflicts with BLE callbacks)
 * 8. Tank sensors - Initial reading
 * 9. BLE scan - Start looking for devices (last because it's async)
 * 
 * NOTE: setTime() is called BEFORE startScanning() because HTTP requests
 * can interfere with BLE scan callbacks on ESP32-S3.
 */
void setup() 
{
	// ========================================================================
	// SERIAL INITIALIZATION
	// Wait up to 5 seconds for USB CDC serial to connect (for debugging)
	// ========================================================================
	//#ifdef SERIALLOGGER
		//Init Serial
		Serial.begin(115200);
		while(!Serial && millis() < 5000) { } // Wait up to 5 seconds for USB CDC to connect
	//#endif
	delay(1000);

	Serial.println("=== PowerMonitor Starting ===");  // Direct serial test

	// ========================================================================
	// DISPLAY INITIALIZATION
	// Show boot screen immediately so user knows device is alive
	// ========================================================================
	logger.log(WARNING,"Booting...");
	layout.init();
	layout.displayData.batteryMode = BATTERY_COMBINED;  // Initialize to combined view
	layout.drawInitialScreen();

	// ========================================================================
	// WIFI INITIALIZATION
	// Connect to strongest available network for time sync and logging
	// ========================================================================
    wifi.startWifi();

	// ========================================================================
	// DATA LOGGING INITIALIZATION
	// Reset sparkline data for fresh start
	// ========================================================================
	powerLogger.reset(&rtc);

	// ========================================================================
	// BLE INITIALIZATION
	// Initialize BLE stack and register all devices we want to connect to
	// Device order matters: BT2=0, SOK1=1, SOK2=2 (for backoff tracking)
	// ========================================================================
	bleManager.init();
	bleManager.registerDevice(&bt2Reader);   // Device index 0
	bleManager.registerDevice(&sokReader1);  // Device index 1
	bleManager.registerDevice(&sokReader2);  // Device index 2
	bleManager.setIndicatorCallback(bleIndicatorCallback);

	// ========================================================================
	// SCREEN CONTROLLER INITIALIZATION
	// Wire up touch callbacks and data sources
	// ========================================================================
	screenController.init(&layout, &screen, &bleManager);
	screenController.setDeviceReaders(&bt2Reader, &sokReader1, &sokReader2);
	screenController.setTankReaders(&waterTank, &gasTank);

	// ========================================================================
	// TIME SYNCHRONIZATION
	// CRITICAL: Do this BEFORE starting BLE scan!
	// HTTP requests conflict with BLE scan callbacks on ESP32-S3
	// ========================================================================
	setTime();	

	// ========================================================================
	// TANK SENSOR INITIALIZATION
	// Read initial values before BLE starts (ADC can conflict with BLE)
	// ========================================================================
	waterTank.readWaterLevel();
	waterTank.updateDaysRemaining();

	gasTank.readGasLevel();
	gasTank.updateDaysRemaining();	

	// ========================================================================
	// START BLE SCANNING
	// Now safe to start async BLE operations
	// ========================================================================
	delay(1000);
	logger.log(WARNING,"Starting BLE scan for the first time");	
	bleManager.startScanning();

	logger.sendLogs(wifi.isConnected());
}

/*
 * setTime() - Synchronize RTC with internet time server
 * 
 * Uses RapidAPI world-time-api to get current time for Pacific timezone.
 * The ESP32Time library manages the RTC with timezone offset.
 * 
 * RESPONSE FORMAT (from world-time-api3.p.rapidapi.com):
 * {
 *   "unixtime": 1706123456,     // Unix epoch seconds (UTC)
 *   "raw_offset": -28800,       // Pacific Standard Time offset (-8 hours)
 *   "dst_offset": 0             // Daylight savings offset (0 or 3600)
 * }
 * 
 * The epoch time plus offsets gives us correct local time.
 * Called every 30 minutes to keep RTC accurate.
 */
void setTime()
{
	// Ensure WiFi is connected for HTTP request
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

/*
 * loop() - Main processing loop, called continuously by Arduino framework
 * 
 * PROCESSING ORDER (each iteration):
 * 1. Screen polling - Handle touch events
 * 2. BLE polling - Process connections, timeouts, health checks
 * 3. Early exit if BLE waiting - Don't block BLE operations
 * 4. Process new BLE data - Update values from device readers
 * 5. WiFi health check - Reconnect if disconnected (every 60s)
 * 6. SOC logging - Remote log battery status (every 60s)
 * 7. Screen updates - Refresh display values (every 500ms)
 * 8. Power logging - Update sparklines (every 1s)
 * 9. Tank sensors - Read water/gas levels (every 5 min, BLE off)
 * 10. RTC sync - Update time from internet (every 30 min)
 * 11. BLE commands - Send data requests to devices (every 500ms)
 * 12. Log flushing - Send cached logs to Papertrail
 * 
 * TIMING CONFLICTS:
 * - Tank reads require WiFi off (GPIO13/ADC conflict)
 * - Tank reads done with BLE off to avoid timeouts
 * - Time sync done during tank read window for efficiency
 * 
 * BLE COMMAND CYCLING:
 * - Devices polled round-robin: BT2 -> SOK1 -> SOK2 -> repeat
 * - Only connected devices receive commands
 * - BT2 needs startup command before data commands
 */
void loop() 
{
	// ========================================================================
	// STEP 1: SCREEN POLLING
	// Process touch events and brightness management
	// ========================================================================
	screen.poll();
	
	// ========================================================================
	// STEP 2: BLE POLLING
	// Process pending connections, timeouts, and periodic health checks
	// See BLEManager::poll() for detailed documentation
	// ========================================================================
	bleManager.poll();

	// ========================================================================
	// STEP 3: EARLY EXIT IF BLE BUSY
	// If waiting for BLE connection or response, yield to BLE stack
	// Don't process other tasks that might interfere
	// ========================================================================
	if(bleManager.isWaiting())
	{
		delay(1);  // Yield to BLE stack
		return;    // Return early - don't process other tasks while waiting
	}

	// ========================================================================
	// STEP 4: PROCESS NEW BLE DATA
	// Check each device reader for new data and update values
	// incrementHertzCount() tracks data rate for display
	// ========================================================================
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

	// ========================================================================
	// STEP 5: WIFI HEALTH CHECK (every 60 seconds)
	// Reconnect if disconnected - needed for logging and time sync
	// ========================================================================
	if(millis() - lastWifiCheckTime > WIFI_CHECK_TIME) {
		if(!wifi.isConnected()) {
			logger.log(WARNING, "WiFi is currently disconnected, attempting reconnection...");
			wifi.startWifi();
		}
		lastWifiCheckTime = millis();
	}

	// ========================================================================
	// STEP 6: SOC LOGGING (every 60 seconds)
	// Remote log battery status for debugging and monitoring
	// Includes: SOC, amps, isCurrent (stale check), isConnected
	// ========================================================================
	if(millis() - lastSocLogTime > SOC_LOG_TIME) {
		logger.log(INFO, "SOC: SOK1=%d/%fA (curr=%d, conn=%d), SOK2=%d/%fA (curr=%d, conn=%d), BT2=%fV/%fA/%fA (curr=%d, conn=%d)", 
			sokReader1.getSoc(), sokReader1.getAmps(), sokReader1.isCurrent(), sokReader1.isConnected(),
			sokReader2.getSoc(), sokReader2.getAmps(), sokReader2.isCurrent(), sokReader2.isConnected(),
			bt2Reader.getTemperature(), bt2Reader.getSolarAmps(), bt2Reader.getAlternaterAmps(), bt2Reader.isCurrent(), bt2Reader.isConnected());
		lastSocLogTime = millis();
	}

	// ========================================================================
	// STEP 7: SCREEN UPDATES (every 500ms)
	// Update display values, indicators, and track usage
	// ========================================================================
	screenController.update(&rtc, wifi.isConnected());

	// ========================================================================
	// STEP 8: POWER LOGGING (every 1 second)
	// Add current amp draw/charge to sparkline data
	// Positive = charging, Negative = discharging
	// ========================================================================
	if(millis()>lastPwrUpdateTime+PWR_UPD_TIME)
	{
		lastPwrUpdateTime=millis();
		// Net amps = charge amps minus draw amps (positive = net charge)
		powerLogger.add(layout.displayData.chargeAmps-layout.displayData.drawAmps,&rtc,&layout);
		
		delay(50); // Delay after screen refresh to avoid power spike 
	}	

	// ========================================================================
	// STEP 9: TANK SENSORS (every 5 minutes)
	// Read water and gas tank levels
	// 
	// IMPORTANT: This requires WiFi OFF due to GPIO13/ADC conflict on ESP32-S3
	// Also turn BLE OFF to avoid timeouts during blocking ADC operations
	// ========================================================================
	if(millis() - lastTankCheckTime > TANK_CHECK_TIME) 
	{
		// Stop BLE first to avoid timeouts during blocking operations
		bleManager.turnOff();
		
		// Stop WiFi to release GPIO13 for ADC
		if(wifi.isConnected()) {
			wifi.stopWifi();
			delay(100);  // Brief delay for WiFi to fully stop
		}
		
		// Read tank sensors (blocking ADC operations)
		waterTank.readWaterLevel();
		waterTank.updateDaysRemaining();

		gasTank.readGasLevel();
		gasTank.updateDaysRemaining();		

		// Restore WiFi
		wifi.startWifi();
		delay(100);  // Brief delay for WiFi to start
		
		// Restart BLE after WiFi is back up
		bleManager.turnOn();
		
		lastTankCheckTime = millis();
	}

	// ========================================================================
	// STEP 10: RTC TIME SYNC (every 30 minutes)
	// Refresh system time from internet time server
	// ========================================================================
	if((lastRTCUpdateTime+REFRESH_RTC) < millis())
	{
		lastRTCUpdateTime=millis();
		setTime();
	}

	// ========================================================================
	// STEP 11: BLE COMMAND CYCLING (every 500ms)
	// Send data requests to BLE devices in round-robin order
	// 
	// Cycle order: BT2 (0) -> SOK1 (1) -> SOK2 (2) -> repeat
	// Only sends to connected devices
	// BT2 needs startup command first, then alternates solar/alternator data
	// ========================================================================
	static int deviceCycle = 0;  // Persists across loop iterations
	BLE_SEMAPHORE* bleSemaphore = bleManager.getSemaphore();
	if(millis() - lastCheckedTime > POLL_TIME_MS)
	{
		lastCheckedTime = millis();
		
		// Device 0: BT2 Renogy charge controller
		if (deviceCycle == 0 && bt2Reader.isConnected()) 
		{
			if (bt2Reader.needsStartupCommand()) {
				bt2Reader.sendStartupCommand(bleSemaphore);  // First command after connect
			} else {
				bt2Reader.sendSolarOrAlternaterCommand(bleSemaphore);  // Alternate data requests
			}
		}
		// Device 1: SOK Battery 1
		else if (deviceCycle == 1 && sokReader1.isConnected()) 
		{
			sokReader1.sendReadCommand(bleSemaphore);
		}
		// Device 2: SOK Battery 2
		else if (deviceCycle == 2 && sokReader2.isConnected()) 
		{
			sokReader2.sendReadCommand(bleSemaphore);
		}
		
		// Advance to next device for next iteration
		deviceCycle = (deviceCycle + 1) % 3;
	}
	else
	{
		// ====================================================================
		// STEP 12: LOG FLUSHING
		// When not sending BLE commands, flush cached logs to Papertrail
		// ====================================================================
		logger.sendLogs(wifi.isConnected());	
		delay(50); // Delay to avoid power spike
	}

	// ========================================================================
	// CRITICAL: YIELD TO BLE STACK
	// Without this delay, the busy loop can starve the NimBLE host task
	// and BLE notifications won't be delivered properly
	// ========================================================================
	delay(1);
}
