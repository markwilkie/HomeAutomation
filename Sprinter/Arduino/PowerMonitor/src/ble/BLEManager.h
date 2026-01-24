#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <NimBLEDevice.h>
#include "BTDevice.hpp"
#include "BT2Reader.h"
#include "SOKReader.h"
#include "../logging/logger.h"

extern Logger logger;

// Timing constants
#define BT_TIMEOUT_MS       15000   // BLE response timeout
#define BLE_RECONNECT_TIME  60000   // Restart BLE scan every 60 seconds if not all devices connected
#define BLE_STARTUP_TIMEOUT 120000  // Full BLE restart if not all devices connect within 2 minutes
#define BLE_CONNECT_THROTTLE_MS 2000  // Minimum ms between connection attempts
#define BLE_IS_ALIVE_TIME   30000   // Check for disconnected devices every 30 seconds
#define BLE_MAX_RETRIES     5       // Max connection retries before entering backoff
#define BLE_BACKOFF_TIME    1800000 // 30 minutes backoff after max retries

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
    
    // Per-device retry tracking
    int deviceRetryCount[MAX_DEVICES] = {0};           // Failed connection attempts
    unsigned long deviceBackoffUntil[MAX_DEVICES] = {0}; // Timestamp when backoff ends
    
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
