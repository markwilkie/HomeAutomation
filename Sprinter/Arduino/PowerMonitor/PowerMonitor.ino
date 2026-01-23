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
#define SCR_UPDATE_TIME 500
#define BITMAP_UPDATE_TIME 5000
#define BLE_IS_ALIVE_TIME 30000
// Give peripherals more time to deliver the first notify after subscribe/write.
#define BT_TIMEOUT_MS	15000
#define TANK_CHECK_TIME 300000       // Check gas and water tank every 5 minutes
#define WIFI_CHECK_TIME 60000       // Check WiFi connection every 1 minute
#define SOC_LOG_TIME 60000          // Log SOC from each battery every 1 minute
#define BLE_RECONNECT_TIME 60000    // Restart BLE scan every 60 seconds if not all devices connected
#define BLE_STARTUP_TIMEOUT 120000   // Full BLE restart if not all devices connect within 2 minutes
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
GasTank gasTank;

//state
BLE_SEMAPHORE bleSemaphore = {nullptr, 0, 0, false, false};  // Initialize all fields

// Connection state - matches official NimBLE example pattern
// NOTE: Do not keep a raw NimBLEAdvertisedDevice pointer; it can become stale and corrupt addresses.
static NimBLEAddress advAddress;               // Copied address of device to connect to
static std::string advName;                    // Copied name of device to connect to
static bool haveAdvDevice = false;             // True when advAddress/advName are valid
static volatile bool doConnect = false;        // Flag to trigger connection from loop
static BTDevice* targetDevice = nullptr;       // Our device handler

long lastRTCUpdateTime=0;
long lastScrUpdatetime=0;
long lastBitmapUpdatetime=0;
long lastBleIsAliveTime=0;
long lastPwrUpdateTime=0;
long lastTankCheckTime=0;
long lastWifiCheckTime=0;
long lastSocLogTime=0;
long lastBleReconnectTime=0;
long lastBleConnectAttempt=0;  // Throttle connection attempts
long bleStartupTime=0;         // Track when BLE was started for startup timeout
#define BLE_CONNECT_THROTTLE_MS 2000  // Minimum ms between connection attempts
int renogyCmdSequenceToSend=0;
long lastCheckedTime=0;
long hertzTime=0;
int hertzCount=0;

// NimBLE scan and client callback handlers - NimBLE 2.x API
class MyScanCallbacks : public NimBLEScanCallbacks {
public:
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override;
    void onScanEnd(const NimBLEScanResults& results, int reason) override;
};

class MyClientCallbacks : public NimBLEClientCallbacks {
public:
    void onConnect(NimBLEClient* pClient) override;
    void onDisconnect(NimBLEClient* pClient, int reason) override;
};

static MyScanCallbacks scanCallbacks;
static MyClientCallbacks clientCallbacks;
static NimBLEScan* pBLEScan = nullptr;
static bool scanningEnabled = true;

// Forward declarations
void bt2NotifyCallback(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify);
void sok1NotifyCallback(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify);
void sok2NotifyCallback(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify);
void handleConnection(NimBLEClient* pClient, NimBLEAddress address);
bool connectToServer();

void startBLE()
{
	logger.log(INFO,"NimBLE (via ESP32-S3) connecting to Renogy BT-2 and SOK batteries");
	NimBLEDevice::init("");
	NimBLEDevice::setPower(3);  // 3 dBm like official example
	
	pBLEScan = NimBLEDevice::getScan();
	// Use callbacks like official example - no duplicates (false)
	pBLEScan->setScanCallbacks(&scanCallbacks, false);
	pBLEScan->setActiveScan(true);
	pBLEScan->setInterval(100);
	pBLEScan->setWindow(100);  // Match interval like official example
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

	//Start BLE
	startBLE();

	//Set touch callback functions
	screen.addTouchCallback(&screenTouchedCallback);
	screen.addLongTouchCallback(&longScreenTouchedCallback);

	//setting time BEFORE starting scan (avoids scan callback during HTTP request)
	setTime();	

	// start scanning for peripherals
	delay(2000);
	logger.log(WARNING,"Starting BLE scan for the first time");	
	scanningEnabled = true;
	pBLEScan->start(0, false, false);  // Scan continuously

	//set time to current so it waits before checking the first time
	lastBleIsAliveTime=millis();
	lastBitmapUpdatetime=millis();

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

/** Scan callback - matches official NimBLE example pattern exactly */
void MyScanCallbacks::onResult(const NimBLEAdvertisedDevice* advertisedDevice) 
{
	// Match official example: use Serial.printf instead of heavy logger
	Serial.printf("BLE: Found device: %s\n", advertisedDevice->getName().c_str());
	
	// Check if this is one of our target devices
	std::string name = advertisedDevice->getName();
	BTDevice* matched = nullptr;
	
	if(!bt2Reader.isConnected() && name == bt2Reader.getPerifpheryName()) {
		matched = &bt2Reader;
	}
	else if(!sokReader1.isConnected() && name == sokReader1.getPerifpheryName()) {
		matched = &sokReader1;
	}
	else if(!sokReader2.isConnected() && name == sokReader2.getPerifpheryName()) {
		matched = &sokReader2;
	}
	
	if(matched != nullptr) {
		Serial.printf("BLE: Target device found! Stopping scan.\n");
		// Match official example exactly:
		NimBLEDevice::getScan()->stop();
		advAddress = advertisedDevice->getAddress();
		advName = advertisedDevice->getName();
		haveAdvDevice = true;
		targetDevice = matched;
		doConnect = true;
	}
}

/** Scan end callback */
void MyScanCallbacks::onScanEnd(const NimBLEScanResults& results, int reason) 
{
	Serial.printf("BLE: Scan ended, reason=%d, count=%d\n", reason, results.getCount());
	// Restart scan if not connecting
	if(!doConnect && scanningEnabled) {
		NimBLEDevice::getScan()->start(0, false, true);
	}
}

/** Connect to server - matches official NimBLE example pattern */
bool connectToServer()
{
	if(!haveAdvDevice || targetDevice == nullptr) {
		logger.log(ERROR, "BLE: No device to connect to!");
		return false;
	}

	// Copy locally, then clear the shared scan result to avoid using stale data.
	NimBLEAddress connectAddress = advAddress;
	std::string connectName = advName;
	haveAdvDevice = false;
	
	logger.log(INFO, "BLE: Connecting to %s (%s)...", connectName.c_str(), connectAddress.toString().c_str());
	
	NimBLEClient* pClient = nullptr;
	
	// Check if we have a client we should reuse (official example pattern)
	if(NimBLEDevice::getCreatedClientCount()) {
		pClient = NimBLEDevice::getClientByPeerAddress(connectAddress);
		if(pClient) {
			// Match NimBLE 2.x client example: skip full service refresh on reconnect
			if(!pClient->connect(connectAddress, false)) {
				logger.log(ERROR, "BLE: Reconnect failed");
				return false;
			}
			//logger.log(INFO, "BLE: Reconnected to existing client");
		} else {
			pClient = NimBLEDevice::getDisconnectedClient();
		}
	}
	
	// No client to reuse? Create new one
	if(!pClient) {
		pClient = NimBLEDevice::createClient();
		if(!pClient) {
			logger.log(ERROR, "BLE: Failed to create client");
			return false;
		}
		//logger.log(INFO, "BLE: New client created");
		
		pClient->setClientCallbacks(&clientCallbacks, false);
		// Looser connection params - some peripherals can't handle tight timing
		// Min/Max interval: 24*1.25=30ms to 48*1.25=60ms, latency 0, timeout 400*10=4000ms
		pClient->setConnectionParams(24, 48, 0, 400);
		pClient->setConnectTimeout(5000);
		
		if(!pClient->connect(connectAddress)) {
			NimBLEDevice::deleteClient(pClient);
			logger.log(ERROR, "BLE: Failed to connect, deleted client");
			return false;
		}
	}
	
	if(!pClient->isConnected()) {
		if(!pClient->connect(connectAddress)) {
			logger.log(ERROR, "BLE: Failed to connect");
			return false;
		}
	}
	
	//logger.log(INFO, "BLE: Connected to %s, RSSI=%d", 
	//	pClient->getPeerAddress().toString().c_str(), pClient->getRssi());
	
	// Set up semaphore
	bleSemaphore.btDevice = targetDevice;
	bleSemaphore.waitingForConnection = true;
	bleSemaphore.waitingForResponse = false;
	bleSemaphore.startTime = millis();
	
	// Copy address to device
	const uint8_t* addrVal = connectAddress.getVal();
	memcpy(targetDevice->getPeripheryAddress(), addrVal, 6);
	
	// Handle the connection (service discovery, subscribe, etc)
	handleConnection(pClient, connectAddress);
	
	return true;
}

// Handle connection and service discovery - matches official NimBLE example pattern
// Get services FRESH from pClient, subscribe immediately
void handleConnection(NimBLEClient* pClient, NimBLEAddress address)
{
	const uint8_t* addrVal = address.getVal();
	uint8_t addr[6];
	memcpy(addr, addrVal, 6);
	
	//logger.log(INFO, "BLE: handleConnection for %s", address.toString().c_str());
	
	// NimBLE 2.x CRITICAL: Must discover all attributes including CCCD descriptors.
	// Without this, subscribe() may fail to find/write CCCD and notifications won't work.
	// discoverAttributes() does full discovery of services, characteristics, AND descriptors.
	//logger.log(INFO, "BLE: Discovering all attributes...");
	if(!pClient->discoverAttributes()) {
		logger.log(ERROR, "BLE: discoverAttributes() failed!");
		pClient->disconnect();
		return;
	}
	//logger.log(INFO, "BLE: Attribute discovery complete");
	
	// BT2 Reader - Renogy device
	if(memcmp(addr, bt2Reader.getPeripheryAddress(), 6) == 0)
	{
		//logger.log(INFO, "BLE: Processing BT2 connection");
		
		// Get fresh service/characteristic references (like official example)
		NimBLERemoteService* pTxSvc = pClient->getService("ffd0");
		NimBLERemoteService* pRxSvc = pClient->getService("fff0");
		
		if(!pTxSvc || !pRxSvc) {
			logger.log(ERROR, "BLE: BT2 service not found tx=%p rx=%p", pTxSvc, pRxSvc);
			pClient->disconnect();
			return;
		}
		
		NimBLERemoteCharacteristic* pTxChar = pTxSvc->getCharacteristic("ffd1");
		NimBLERemoteCharacteristic* pRxChar = pRxSvc->getCharacteristic("fff1");
		
		if(!pTxChar || !pRxChar) {
			logger.log(ERROR, "BLE: BT2 char not found tx=%p rx=%p", pTxChar, pRxChar);
			pClient->disconnect();
			return;
		}
		
		// Log characteristic properties for debugging
		//logger.log(INFO, "BLE: BT2 RX char props: canRead=%d canWrite=%d canNotify=%d canIndicate=%d", 
		//	pRxChar->canRead(), pRxChar->canWrite(), pRxChar->canNotify(), pRxChar->canIndicate());
		
		// Verify CCCD descriptor exists before subscribing
		NimBLERemoteDescriptor* pCccd = pRxChar->getDescriptor(NimBLEUUID((uint16_t)0x2902));
		if(pCccd) {
			//logger.log(INFO, "BLE: BT2 CCCD found, handle=0x%04X", pCccd->getHandle());
		} else {
			logger.log(WARNING, "BLE: BT2 CCCD (0x2902) NOT FOUND - notifications may not work!");
		}
		
		//logger.log(INFO, "BLE: BT2 services found, subscribing to %s (handle=0x%04X)", 
		//	pRxChar->getUUID().toString().c_str(), pRxChar->getHandle());
		// Subscribe for notifications using static callback (matches official NimBLE example)
		bool subscribed = false;
		if(pRxChar->canNotify()) {
			subscribed = pRxChar->subscribe(true, bt2NotifyCallback);
			//logger.log(INFO, "BLE: BT2 subscribe(notify) => %s", subscribed ? "OK" : "FAILED");
		} else if(pRxChar->canIndicate()) {
			subscribed = pRxChar->subscribe(false, bt2NotifyCallback);
			//logger.log(INFO, "BLE: BT2 subscribe(indicate) => %s", subscribed ? "OK" : "FAILED");
		} else {
			logger.log(ERROR, "BLE: BT2 RX char has no notify/indicate!");
		}
		if(!subscribed) {
			logger.log(ERROR, "BLE: BT2 subscribe FAILED");
			pClient->disconnect();
			return;
		}
		// Give NimBLE time to process subscription internally
		delay(100);
		
		// Verify CCCD was written - read it back
		NimBLERemoteDescriptor* pCccdVerify = pRxChar->getDescriptor(NimBLEUUID((uint16_t)0x2902));
		if(pCccdVerify) {
			NimBLEAttValue cccdVal = pCccdVerify->readValue();
			if(cccdVal.size() >= 2) {
				uint16_t cccdBits = cccdVal[0] | (cccdVal[1] << 8);
				//, "BLE: BT2 CCCD readback = 0x%04X (notify=%d, indicate=%d)", cccdBits, (cccdBits & 1), (cccdBits & 2) >> 1);
			} else {
				logger.log(WARNING, "BLE: BT2 CCCD readback too short: %d bytes", cccdVal.size());
			}
		}
		
		// Store references and mark connected
		bt2Reader.setCharacteristics(pClient, pTxChar, pRxChar);
		bleSemaphore.waitingForConnection = false;
		// Let main loop send first command - don't block here
	}
	// SOK Battery 1
	else if(memcmp(addr, sokReader1.getPeripheryAddress(), 6) == 0)
	{
		//logger.log(INFO, "BLE: Processing SOK1 connection");
		
		// SOK uses same service for tx and rx (ffe0), different characteristics
		NimBLERemoteService* pSvc = pClient->getService("ffe0");
		
		if(!pSvc) {
			logger.log(ERROR, "BLE: SOK1 service ffe0 not found");
			pClient->disconnect();
			return;
		}
		
		NimBLERemoteCharacteristic* pRxChar = pSvc->getCharacteristic("ffe1");
		NimBLERemoteCharacteristic* pTxChar = pSvc->getCharacteristic("ffe2");
		
		if(!pRxChar) {
			logger.log(ERROR, "BLE: SOK1 rx char ffe1 not found");
			pClient->disconnect();
			return;
		}
		
		// Log characteristic properties for debugging
		//logger.log(INFO, "BLE: SOK1 RX char props: canRead=%d canWrite=%d canNotify=%d canIndicate=%d", 
		//	pRxChar->canRead(), pRxChar->canWrite(), pRxChar->canNotify(), pRxChar->canIndicate());
		
		// Verify CCCD descriptor exists before subscribing
		NimBLERemoteDescriptor* pCccd = pRxChar->getDescriptor(NimBLEUUID((uint16_t)0x2902));
		if(pCccd) {
			//logger.log(INFO, "BLE: SOK1 CCCD found, handle=0x%04X", pCccd->getHandle());
		} else {
			logger.log(WARNING, "BLE: SOK1 CCCD (0x2902) NOT FOUND - notifications may not work!");
		}
		
		//logger.log(INFO, "BLE: SOK1 service found, subscribing to %s", pRxChar->getUUID().toString().c_str());
		
		// Subscribe using static callback (matches official NimBLE example)
		bool sok1Subscribed = false;
		if(pRxChar->canNotify()) {
			sok1Subscribed = pRxChar->subscribe(true, sok1NotifyCallback);
			//logger.log(INFO, "BLE: SOK1 subscribe(notify) => %s", sok1Subscribed ? "OK" : "FAILED");
		} else if(pRxChar->canIndicate()) {
			sok1Subscribed = pRxChar->subscribe(false, sok1NotifyCallback);
			//logger.log(INFO, "BLE: SOK1 subscribe(indicate) => %s", sok1Subscribed ? "OK" : "FAILED");
		} else {
			logger.log(ERROR, "BLE: SOK1 RX char has no notify/indicate!");
		}
		if(!sok1Subscribed) {
			logger.log(ERROR, "BLE: SOK1 subscribe FAILED");
			pClient->disconnect();
			return;
		}
		
		// Verify CCCD was written - read it back
		NimBLERemoteDescriptor* pCccdVerify = pRxChar->getDescriptor(NimBLEUUID((uint16_t)0x2902));
		if(pCccdVerify) {
			NimBLEAttValue cccdVal = pCccdVerify->readValue();
			if(cccdVal.size() >= 2) {
				uint16_t cccdBits = cccdVal[0] | (cccdVal[1] << 8);
				//logger.log(INFO, "BLE: SOK1 CCCD readback = 0x%04X (notify=%d, indicate=%d)", 
				//	cccdBits, (cccdBits & 1), (cccdBits & 2) >> 1);
			} else {
				logger.log(WARNING, "BLE: SOK1 CCCD readback too short: %d bytes", cccdVal.size());
			}
		}
		
		bleSemaphore.waitingForConnection = false;  // Clear BEFORE setting characteristics
		sokReader1.setCharacteristics(pClient, pTxChar, pRxChar);
		// Let main loop send first command - don't block here
	}
	// SOK Battery 2
	else if(memcmp(addr, sokReader2.getPeripheryAddress(), 6) == 0)
	{
		//logger.log(INFO, "BLE: Processing SOK2 connection");
		
		NimBLERemoteService* pSvc = pClient->getService("ffe0");
		
		if(!pSvc) {
			logger.log(ERROR, "BLE: SOK2 service ffe0 not found");
			pClient->disconnect();
			return;
		}
		
		NimBLERemoteCharacteristic* pRxChar = pSvc->getCharacteristic("ffe1");
		NimBLERemoteCharacteristic* pTxChar = pSvc->getCharacteristic("ffe2");
		
		if(!pRxChar) {
			logger.log(ERROR, "BLE: SOK2 rx char ffe1 not found");
			pClient->disconnect();
			return;
		}
		if(!pTxChar) {
			logger.log(ERROR, "BLE: SOK2 tx char ffe2 not found");
			pClient->disconnect();
			return;
		}
		
		// Log characteristic properties for debugging
		//logger.log(INFO, "BLE: SOK2 RX char props: canRead=%d canWrite=%d canNotify=%d canIndicate=%d", 
		//	pRxChar->canRead(), pRxChar->canWrite(), pRxChar->canNotify(), pRxChar->canIndicate());
		//logger.log(INFO, "BLE: SOK2 TX char props: canRead=%d canWrite=%d canWriteNoResponse=%d", 
		//	pTxChar->canRead(), pTxChar->canWrite(), pTxChar->canWriteNoResponse());
		
		// Verify CCCD descriptor exists before subscribing
		NimBLERemoteDescriptor* pCccd = pRxChar->getDescriptor(NimBLEUUID((uint16_t)0x2902));
		if(pCccd) {
			//logger.log(INFO, "BLE: SOK2 CCCD found, handle=0x%04X", pCccd->getHandle());
		} else {
			logger.log(WARNING, "BLE: SOK2 CCCD (0x2902) NOT FOUND - notifications may not work!");
		}
		
		//logger.log(INFO, "BLE: SOK2 service found, subscribing to %s", pRxChar->getUUID().toString().c_str());
		
		// Subscribe using static callback (matches official NimBLE example)
		bool sok2Subscribed = false;
		if(pRxChar->canNotify()) {
			sok2Subscribed = pRxChar->subscribe(true, sok2NotifyCallback);
			//logger.log	(INFO, "BLE: SOK2 subscribe(notify) => %s", sok2Subscribed ? "OK" : "FAILED");
		} else if(pRxChar->canIndicate()) {
			sok2Subscribed = pRxChar->subscribe(false, sok2NotifyCallback);
			//logger.log(INFO, "BLE: SOK2 subscribe(indicate) => %s", sok2Subscribed ? "OK" : "FAILED");
		} else {
			logger.log(ERROR, "BLE: SOK2 RX char has no notify/indicate!");
		}
		if(!sok2Subscribed) {
			logger.log(ERROR, "BLE: SOK2 subscribe FAILED");
			pClient->disconnect();
			return;
		}
		
		// Verify CCCD was written - read it back
		NimBLERemoteDescriptor* pCccdVerify = pRxChar->getDescriptor(NimBLEUUID((uint16_t)0x2902));
		if(pCccdVerify) {
			NimBLEAttValue cccdVal = pCccdVerify->readValue();
			if(cccdVal.size() >= 2) {
				uint16_t cccdBits = cccdVal[0] | (cccdVal[1] << 8);
				//logger.log(INFO, "BLE: SOK2 CCCD readback = 0x%04X (notify=%d, indicate=%d)", 
				//	cccdBits, (cccdBits & 1), (cccdBits & 2) >> 1);
			} else {
				logger.log(WARNING, "BLE: SOK2 CCCD readback too short: %d bytes", cccdVal.size());
			}
		}
		
		bleSemaphore.waitingForConnection = false;  // Clear BEFORE setting characteristics
		sokReader2.setCharacteristics(pClient, pTxChar, pRxChar);
		// Let main loop send first command - don't block here
	}
	
	// Update BLE indicator
	layout.setBLEIndicator(TFT_YELLOW);
	
	// Should I stop scanning now?
	boolean stopScanning = true;
	int numOfTargetedDevices = sizeof(targetedDevices) / sizeof(targetedDevices[0]);
	for(int i = 0; i < numOfTargetedDevices; i++)
	{
		if(!targetedDevices[i]->isConnected())
			stopScanning = false;
	}
	if(stopScanning)
	{
		layout.setBLEIndicator(TFT_BLUE);
		logger.log(INFO, "All devices connected. Showing online and stopping scan");
		scanningEnabled = false;
		pBLEScan->stop();
	}
	else if(scanningEnabled)
	{
		// Resume scanning for other devices
		pBLEScan->start(0, false, false);
	}
}

// NimBLE disconnect callback
void MyClientCallbacks::onConnect(NimBLEClient* pClient) 
{
	// Connection handling done in handleConnection()
}

void MyClientCallbacks::onDisconnect(NimBLEClient* pClient, int reason) 
{
	logger.log(WARNING, "BLE: onDisconnect called, reason=%d", reason);
	
	bleSemaphore.waitingForConnection = false;
	bleSemaphore.waitingForResponse = false;
	bleSemaphore.btDevice = nullptr;
	
	// Determine which device disconnected
	NimBLEAddress addr = pClient->getPeerAddress();
	const uint8_t* addrVal = addr.getVal();
	uint8_t addrBytes[6];
	memcpy(addrBytes, addrVal, 6);
	
	if(memcmp(addrBytes, bt2Reader.getPeripheryAddress(), 6) == 0)
	{
		renogyCmdSequenceToSend = 0;
		bt2Reader.disconnectCallback(pClient);
		logger.log(WARNING, "BLE: Disconnected BT2 device %s (reason=%d)", addr.toString().c_str(), reason);
	}
	else if(memcmp(addrBytes, sokReader1.getPeripheryAddress(), 6) == 0)
	{
		sokReader1.disconnectCallback(pClient);
		logger.log(WARNING, "BLE: Disconnected SOK Battery 1 %s (reason=%d)", addr.toString().c_str(), reason);
	}
	else if(memcmp(addrBytes, sokReader2.getPeripheryAddress(), 6) == 0)
	{
		sokReader2.disconnectCallback(pClient);
		logger.log(WARNING, "BLE: Disconnected SOK Battery 2 %s (reason=%d)", addr.toString().c_str(), reason);
	}
	else
	{
		logger.log(WARNING, "BLE: Disconnected unknown device %s (reason=%d)", addr.toString().c_str(), reason);
	}
	
	layout.setBLEIndicator(TFT_DARKGRAY);
	logger.log(INFO, "BLE: Restarting scan after disconnect");
	scanningEnabled = true;
	pBLEScan->start(0, false, false);
}

// Notification callbacks for each device - keep minimal, runs in BLE context
void bt2NotifyCallback(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify)
{
	Serial.printf(">>> BT2 NOTIFY: len=%d\n", length);
	bt2Reader.notifyCallback(pRemoteCharacteristic, pData, length, &bleSemaphore);
}

void sok1NotifyCallback(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify)
{
	Serial.printf(">>> SOK1 NOTIFY: len=%d\n", length);
	sokReader1.notifyCallback(pRemoteCharacteristic, pData, length, &bleSemaphore);
}

void sok2NotifyCallback(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify)
{
	Serial.printf(">>> SOK2 NOTIFY: len=%d\n", length);
	sokReader2.notifyCallback(pRemoteCharacteristic, pData, length, &bleSemaphore);
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
	logger.log(WARNING,"Turning off BLE");
	layout.setBLEIndicator(TFT_BLACK);
	scanningEnabled = false;
	pBLEScan->stop();
	
	// Disconnect all clients
	int numOfTargetedDevices = sizeof(targetedDevices) / sizeof(targetedDevices[0]);
	for(int i = 0; i < numOfTargetedDevices; i++)
	{
		if(targetedDevices[i]->isConnected())
		{
			targetedDevices[i]->disconnect();
		}
	}
}

void turnOnBLE()
{
	layout.setBLEIndicator(TFT_DARKGRAY);
	logger.log(WARNING,"Starting BLE and scanning");
	scanningEnabled = true;
	bleStartupTime = millis();  // Track when BLE started for startup timeout
	lastBleReconnectTime = millis();  // Reset reconnect timer too
	pBLEScan->start(0, false, false);
}

void longScreenTouchedCallback(int x,int y)
{
	//check if in the BLE region - meaning, should we toggle BLE on/off
	if(layout.isBLERegion(x,y))
	{
		static bool BLEOn=true;
		BLEOn=!BLEOn;
		logger.log(WARNING,"BLE Toggled %d (%d,%d)",BLEOn,x,y);
		if(!BLEOn)
		{
			disconnectBLE();
		}
		else
		{
			startBLE();
		}
	}
}

void disconnectBLE()
{
	layout.setBLEIndicator(TFT_BLACK);
	scanningEnabled = false;
	pBLEScan->stop();
	logger.log(WARNING,"Disconnecting all BLE devices");

	int numOfTargetedDevices=sizeof(targetedDevices)/(sizeof(targetedDevices[0]));
	for(int i=0;i<numOfTargetedDevices;i++)
	{
		//logger.log(INFO,targetedDevices[i]->getPerifpheryName());
		//logger.log(INFO,targetedDevices[i]->isConnected());
		if(targetedDevices[i]->isConnected())
			targetedDevices[i]->disconnect();
	}
}

// Check if any targeted BLE devices are not connected and restart scan periodically
void checkForDisconnectedDevices()
{
	int numOfTargetedDevices = sizeof(targetedDevices) / sizeof(targetedDevices[0]);
	bool allConnected = true;
	bool anyStale = false;
	
	for(int i = 0; i < numOfTargetedDevices; i++)
	{
		if(!targetedDevices[i]->isConnected())
		{
			allConnected = false;
		}
		// Check for stale data - device is connected but not receiving data
		else if(!targetedDevices[i]->isCurrent())
		{
			anyStale = true;
			logger.log(ERROR, "BLE: Device %s is stale (connected but no data), disconnecting...", 
				targetedDevices[i]->getPerifpheryName());
			targetedDevices[i]->disconnect();
			allConnected = false;
		}
	}
	
	// If all connected and none stale, reset startup timer so we don't do hard restart later
	if(allConnected && !anyStale)
	{
		bleStartupTime = millis();
		return;
	}
	
	// If startup timeout exceeded and not all devices connected, do full BLE restart
	if(millis() - bleStartupTime > BLE_STARTUP_TIMEOUT)
	{
		logger.log(WARNING, "BLE startup timeout - not all devices connected. Restarting BLE...");
		turnOffBLE();
		delay(500);
		turnOnBLE();
		return;
	}
	
	// If not all devices connected, restart BLE scan periodically
	if(millis() - lastBleReconnectTime > BLE_RECONNECT_TIME)
	{
		logger.log(WARNING, "Not all BLE devices connected, restarting scan...");
		scanningEnabled = true;
		pBLEScan->stop();
		delay(100);
		pBLEScan->start(0, false, false);
		lastBleReconnectTime = millis();
	}
}

void loop() 
{
	screen.poll();
	
	// Process pending connection - matches official NimBLE example pattern
	if(doConnect) {
		doConnect = false;
		if(connectToServer()) {
			//logger.log(INFO, "BLE: Connection successful!");
		} else {
			logger.log(WARNING, "BLE: Connection failed, restarting scan");
		}
		// Restart scanning for more devices
		NimBLEDevice::getScan()->start(0, false, true);
	}

	//check for BLE timeouts - and disconnect
	if(isTimedout())
	{
		bleSemaphore.waitingForResponse=false;

		/*
		logger.log(WARNING,"BLE operation timed out, disconnecting then restarting");
		if(bleSemaphore.btDevice)
			bleSemaphore.btDevice->disconnect();
		bleSemaphore.waitingForConnection=false;
		bleSemaphore.waitingForResponse=false;
		bleSemaphore.btDevice=nullptr;
		
		// Delay to let BLE stack settle before restarting scan
		delay(50);
		
		// Restart scanning to give failed device another chance
		scanningEnabled = true;
		pBLEScan->start(0, false, false);
		*/
	}

	//time to update power logs?
	if(millis()>lastPwrUpdateTime+PWR_UPD_TIME)
	{
		lastPwrUpdateTime=millis();
		powerLogger.add(layout.displayData.chargeAmps-layout.displayData.drawAmps,&rtc,&layout);
	}

	if(millis()>lastBleIsAliveTime+BLE_IS_ALIVE_TIME) {
		checkForDisconnectedDevices();

		lastBleIsAliveTime=millis();
	}

	// Check WiFi connection every 60 seconds and reconnect if needed
	// Stop BLE during WiFi reconnection to avoid timeouts from blocking operations
	if(millis() - lastWifiCheckTime > WIFI_CHECK_TIME &&
	   !bleSemaphore.waitingForConnection && !bleSemaphore.waitingForResponse) {
		if(!wifi.isConnected()) {
			logger.log(WARNING, "WiFi is currently disconnected, attempting reconnection...");
			turnOffBLE();
			wifi.startWifi();
			turnOnBLE();
		}
		lastWifiCheckTime = millis();
	}

	// Log SOC from each battery every 1 minute
	if(millis() - lastSocLogTime > SOC_LOG_TIME) {
		logger.log(INFO, "SOC: SOK1=%d (curr=%d, conn=%d), SOK2=%d (curr=%d, conn=%d)", 
			sokReader1.getSoc(), sokReader1.isCurrent(), sokReader1.isConnected(),
			sokReader2.getSoc(), sokReader2.isCurrent(), sokReader2.isConnected());
		lastSocLogTime = millis();
	}

	//load values
	if(millis()>lastScrUpdatetime+SCR_UPDATE_TIME)
	{
		delay(1);
		lastScrUpdatetime=millis();
		//loadSimulatedValues();

		loadValues();
		if(currentScreen == MAIN_SCREEN)
		{
			layout.updateLCD(&rtc);
		}
		layout.setWifiIndicator(wifi.isConnected());

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
			delay(1);
			layout.updateBitmaps();
		}

		//reset time
		lastBitmapUpdatetime=millis();
	}

	// Check gas and water tank every 10 minutes
	// Stop BLE during tank check to avoid timeouts from blocking WiFi operations
	if(millis() - lastTankCheckTime > TANK_CHECK_TIME && 
	   !bleSemaphore.waitingForConnection && !bleSemaphore.waitingForResponse) 
	{
		// Stop BLE first to avoid timeouts during blocking operations
		turnOffBLE();
		
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
		turnOnBLE();
		
		lastTankCheckTime = millis();
	}

	//Time to ask for data again? Cycle through devices
	// Only send if not waiting for a response (semaphore is free)
	static int deviceCycle = 0;
	if (millis()>lastCheckedTime+POLL_TIME_MS && !bleSemaphore.waitingForResponse && !bleSemaphore.waitingForConnection) 
	{
		delay(1);
		lastCheckedTime=millis();
		if (deviceCycle == 0 && bt2Reader.isConnected()) 
		{
			// Send startup command first if needed, otherwise alternate solar/alternator
			if (bt2Reader.needsStartupCommand()) {
				bt2Reader.sendStartupCommand(&bleSemaphore);
			} else {
				bt2Reader.sendSolarOrAlternaterCommand(&bleSemaphore);
			}
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
		delay(1);
		// Verbose logging removed - uncomment for debugging
		//logger.log(INFO,"BT2 New data available");
		hertzCount++;
		bt2Reader.updateValues();
	}
	if(sokReader1.getIsNewDataAvailable())
	{
		delay(1);
		// Verbose logging removed - uncomment for debugging
		//logger.log(INFO,"SOK1 New data available");
		hertzCount++;
		sokReader1.updateValues();
	}
	if(sokReader2.getIsNewDataAvailable())
	{
		delay(1);
		// Verbose logging removed - uncomment for debugging
		//logger.log(INFO,"SOK2 New data available");
		hertzCount++;
		sokReader2.updateValues();
	}

	//Time to refresh rtc?
	// Only run when BLE is idle to avoid blocking BLE operations
	// Stop BLE during time sync since HTTP requests can block
	if((lastRTCUpdateTime+REFRESH_RTC) < millis() && 
	   !bleSemaphore.waitingForConnection && !bleSemaphore.waitingForResponse)
	{
		lastRTCUpdateTime=millis();
		turnOffBLE();
		setTime();
		turnOnBLE();
	}

	//calc hertz
	if(millis()>hertzTime+1000)
	{
		layout.displayData.currentHertz=hertzCount;
		hertzTime=millis();
		hertzCount=0;
	}

	// CRITICAL: Yield to allow NimBLE host task to process BLE events (notifications, etc.)
	// Without this, the busy loop can starve the BLE task and notifications won't be delivered.
	delay(1);
}

boolean isTimedout()
{
	boolean timedOutFlag=false;

	// Only check timeout if startTime is reasonable (set within the last minute)
	// This prevents false timeouts from stale/uninitialized semaphore state
	if(bleSemaphore.startTime == 0 || (millis() - bleSemaphore.startTime) > 60000)
	{
		// Stale semaphore - clear it
		if(bleSemaphore.waitingForConnection || bleSemaphore.waitingForResponse)
		{
			logger.log(WARNING, "Clearing stale BLE semaphore");
			bleSemaphore.waitingForConnection = false;
			bleSemaphore.waitingForResponse = false;
			bleSemaphore.btDevice = nullptr;
		}
		return false;
	}

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
