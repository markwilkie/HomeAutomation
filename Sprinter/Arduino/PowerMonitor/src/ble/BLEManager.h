#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

/*
 * BLEManager - Centralized BLE connection manager for ESP32-S3 with NimBLE 2.x
 * 
 * ARCHITECTURE OVERVIEW:
 * -----------------------
 * This manager handles multiple BLE peripheral devices (BT2 charge controller, SOK batteries).
 * It uses a single scan + connect pattern with per-device retry tracking and backoff.
 * 
 * CONNECTION FLOW:
 * 1. startScanning() begins continuous BLE scan
 * 2. onScanResult() matches found devices against registered targets
 * 3. When target found, sets doConnect=true flag (callback context, can't connect directly)
 * 4. poll() (called from main loop) processes doConnect and calls connectToServer()
 * 5. connectToServer() establishes connection and sets up notifications
 * 6. checkForDisconnectedDevices() monitors health and handles stale/offline devices
 * 
 * RETRY & BACKOFF SYSTEM:
 * -----------------------
 * checkForDisconnectedDevices() is the SINGLE SOURCE OF TRUTH for retry counting.
 * It runs every 30 seconds and handles ALL offline scenarios:
 * 
 * SCENARIO A: Device never found by scanner (powered off, out of range)
 *   - Device stays offline, retry count increments each 30-second check
 *   - After 5 checks (~2.5 min), enters 30-minute backoff
 * 
 * SCENARIO B: Device found but connection fails
 *   - poll() logs the failure but does NOT increment retry count
 *   - Device stays offline, checkForDisconnectedDevices() increments on next check
 *   - Same path as Scenario A from there
 * 
 * SCENARIO C: Device was connected, then disconnects mid-session
 *   - onClientDisconnect() callback fires, device marked offline
 *   - Scan restarts via BLE_RECONNECT_TIME timer (60s)
 *   - If reconnection fails, retry count increments each check
 * 
 * SCENARIO D: Device goes stale (connected but no data)
 *   - checkForDisconnectedDevices() detects via isCurrent() check
 *   - Forces disconnect, resets retry count (device WAS reachable)
 *   - Reconnection proceeds normally
 * 
 * BACKOFF EXPIRATION:
 *   - When backoff expires, counters reset and scanning restarts if needed
 *   - First check cycle after backoff gives scanner a chance before counting
 */

#include <NimBLEDevice.h>
#include "BTDevice.hpp"
#include "BT2Reader.h"
#include "SOKReader.h"
#include "../logging/logger.h"

extern Logger logger;

// ============================================================================
// TIMING CONSTANTS - Tune these to adjust BLE behavior
// ============================================================================
#define BT_TIMEOUT_MS       15000   // Max time to wait for BLE response before giving up
#define BLE_RECONNECT_TIME  60000   // Restart scan every 60s if not all devices connected
#define BLE_STARTUP_TIMEOUT 120000  // Full stack reset if no connections after 2 minutes
#define BLE_CONNECT_THROTTLE_MS 10000 // Min 10 seconds between connection attempts PER DEVICE
                                      // Prevents rapid retry loops when device is found but
                                      // connection fails repeatedly
#define BLE_IS_ALIVE_TIME   30000   // Run health check every 30 seconds
#define BLE_MAX_RETRIES     5       // Max failed attempts before entering backoff
#define BLE_BACKOFF_TIME    1800000 // 30 minutes backoff - stops hammering unavailable device

// Forward declaration
class BLEManager;

// Scan callbacks - NimBLE 2.x API
class BLEMgrScanCallbacks : public NimBLEScanCallbacks {
public:
    BLEMgrScanCallbacks(BLEManager* manager) : bleManager(manager) {}
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override;
    void onScanEnd(const NimBLEScanResults& results, int reason) override;
private:
    BLEManager* bleManager;
};

// Client callbacks - NimBLE 2.x API
class BLEMgrClientCallbacks : public NimBLEClientCallbacks {
public:
    BLEMgrClientCallbacks(BLEManager* manager) : bleManager(manager) {}
    void onConnect(NimBLEClient* pClient) override;
    void onDisconnect(NimBLEClient* pClient, int reason) override;
private:
    BLEManager* bleManager;
};

class BLEManager {
public:
    BLEManager();
    
    // Initialization
    void init();
    void registerDevice(BTDevice* device);
    
    // Main loop functions
    void poll();                    // Call from loop() - handles connections, timeouts, device cycling
    bool isWaiting();               // True if waiting for connection or response
    
    // Control functions
    void startScanning();
    void stopScanning();
    void turnOff();
    void turnOn();
    void resetStack();
    
    // Status
    bool allDevicesConnected();
    bool isScanning() { return scanningEnabled; }
    bool isDeviceInBackoff(int deviceIndex);  // Check if device is in backoff mode
    uint32_t getCurrentIndicatorColor();  // Get current BLE indicator color based on state
    
    // Indicator callback for UI updates
    typedef void (*IndicatorCallback)(uint32_t color);
    void setIndicatorCallback(IndicatorCallback cb) { indicatorCallback = cb; }
    
    // Called by callbacks (public for callback access)
    void onScanResult(const NimBLEAdvertisedDevice* device);
    void onScanEnd(int reason);
    void onClientConnect(NimBLEClient* pClient);
    void onClientDisconnect(NimBLEClient* pClient, int reason);
    
    // Semaphore access for device readers
    BLE_SEMAPHORE* getSemaphore() { return &bleSemaphore; }
    
    // Renogy command sequence (BT2 specific)
    int renogyCmdSequence = 0;

private:
    bool connectToServer();
    void handleConnection(NimBLEClient* pClient, NimBLEAddress address);
    void checkForDisconnectedDevices();
    bool isTimedOut();
    
    // Notify callbacks - static so NimBLE can call them
    static void bt2NotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify);
    static void sok1NotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify);
    static void sok2NotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify);
    
    // Device management
    static const int MAX_DEVICES = 10;
    BTDevice* devices[MAX_DEVICES];
    int deviceCount = 0;
    int deviceCycle = 0;            // For cycling through devices when sending commands
    
    // ========================================================================
    // PER-DEVICE RETRY TRACKING
    // These arrays are indexed by device registration order (same as devices[])
    // ========================================================================
    int deviceRetryCount[MAX_DEVICES] = {0};           // Failed connection attempts (resets on success)
    unsigned long deviceBackoffUntil[MAX_DEVICES] = {0}; // millis() timestamp when backoff ends (0 = not in backoff)
    unsigned long deviceLastAttempt[MAX_DEVICES] = {0};  // millis() of last connection attempt
                                                         // Used by: 1) throttle in onScanResult
                                                         //          2) checkForDisconnectedDevices to know
                                                         //             if scanner is finding device
    
    // BLE state
    BLE_SEMAPHORE bleSemaphore = {nullptr, 0, 0, false, false};
    NimBLEScan* pBLEScan = nullptr;
    bool scanningEnabled = false;
    
    // Connection state - matches official NimBLE example pattern
    NimBLEAddress advAddress;       // Copied address of device to connect to
    std::string advName;            // Copied name of device to connect to
    bool haveAdvDevice = false;     // True when advAddress/advName are valid
    volatile bool doConnect = false;// Flag to trigger connection from loop
    BTDevice* targetDevice = nullptr;
    int targetDeviceIndex = -1;     // Index of device being connected
    
    // Timing
    unsigned long lastBleIsAliveTime = 0;
    unsigned long lastBleReconnectTime = 0;
    unsigned long lastConnectAttempt = 0;
    unsigned long bleStartupTime = 0;
    unsigned long lastPollTime = 0;
    
    // Callbacks
    BLEMgrScanCallbacks scanCallbacks;
    BLEMgrClientCallbacks clientCallbacks;
    IndicatorCallback indicatorCallback = nullptr;
    
    // Static instance pointer for static callbacks
    static BLEManager* instance;
};

#endif
