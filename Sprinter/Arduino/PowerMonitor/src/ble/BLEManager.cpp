#include "BLEManager.h"

/*
 * BLEManager Implementation
 * 
 * See BLEManager.h for architecture overview and retry/backoff documentation.
 * 
 * KEY FUNCTIONS:
 * - poll(): Main loop handler - processes connections, timeouts, runs health checks
 * - onScanResult(): Callback when scanner finds a device - sets doConnect flag
 * - connectToServer(): Actually establishes BLE connection (called from poll())
 * - checkForDisconnectedDevices(): Periodic health check - handles stale/offline devices
 */

// Static instance pointer for static callbacks (NimBLE callbacks are static)
BLEManager* BLEManager::instance = nullptr;

BLEManager::BLEManager() : scanCallbacks(this), clientCallbacks(this) {
    instance = this;
}

void BLEManager::init() {
    logger.log(INFO, "NimBLE (via ESP32-S3) initializing BLE stack");
    NimBLEDevice::init("");
    NimBLEDevice::setPower(3);  // 3 dBm like official example
    
    pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setScanCallbacks(&scanCallbacks, false);
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(100);  // Match interval like official example
}

void BLEManager::registerDevice(BTDevice* device) {
    if(deviceCount < MAX_DEVICES) {
        devices[deviceCount++] = device;
    }
}

void BLEManager::startScanning() {
    logger.log(WARNING, "Starting BLE scan");
    scanningEnabled = true;
    bleStartupTime = millis();
    lastBleReconnectTime = millis();
    lastBleIsAliveTime = millis();  // Reset so we don't immediately check for disconnected devices
    pBLEScan->start(0, false, false);  // Scan continuously
    if(indicatorCallback) indicatorCallback(0x808080);  // Dark gray
}

void BLEManager::stopScanning() {
    scanningEnabled = false;
    pBLEScan->stop();
}

void BLEManager::turnOff() {
    logger.log(WARNING, "Turning off BLE");
    if(indicatorCallback) indicatorCallback(0x000000);  // Black
    stopScanning();
    
    // Disconnect all clients
    for(int i = 0; i < deviceCount; i++) {
        if(devices[i]->isConnected()) {
            devices[i]->disconnect();
        }
    }
}

void BLEManager::turnOn() {
    if(indicatorCallback) indicatorCallback(0x808080);  // Dark gray
    logger.log(WARNING, "Starting BLE and scanning");
    scanningEnabled = true;
    bleStartupTime = millis();
    lastBleReconnectTime = millis();
    lastBleIsAliveTime = millis();  // Reset so we don't immediately check for disconnected devices
    pBLEScan->start(0, false, false);
}

void BLEManager::resetStack() {
    //logger.log(WARNING, "Performing full BLE stack reset");
    
    // Clear semaphore
    bleSemaphore.waitingForConnection = false;
    bleSemaphore.waitingForResponse = false;
    bleSemaphore.btDevice = nullptr;
    renogyCmdSequence = 0;
    
    // Reset stale timers on all devices
    for(int i = 0; i < deviceCount; i++) {
        devices[i]->resetStale();
    }
    
    turnOff();
    delay(500);
    turnOn();
}

bool BLEManager::allDevicesConnected() {
    for(int i = 0; i < deviceCount; i++) {
        if(!devices[i]->isConnected()) {
            return false;
        }
    }
    return true;
}

bool BLEManager::isDeviceInBackoff(int deviceIndex) {
    if(deviceIndex < 0 || deviceIndex >= deviceCount) return false;
    return deviceBackoffUntil[deviceIndex] > 0 && millis() < deviceBackoffUntil[deviceIndex];
}

uint32_t BLEManager::getCurrentIndicatorColor() {
    // Black if BLE is off
    if(!scanningEnabled && !allDevicesConnected()) {
        return 0x000000;  // Black - BLE off
    }
    // Blue if all devices connected
    if(allDevicesConnected()) {
        return 0x0000FF;  // Blue - fully connected
    }
    // Yellow if some devices connected
    for(int i = 0; i < deviceCount; i++) {
        if(devices[i]->isConnected()) {
            return 0xFFFF00;  // Yellow - partially connected
        }
    }
    // Grey if scanning but no connections
    return 0x808080;  // Grey - scanning
}

bool BLEManager::isWaiting() {
    return bleSemaphore.waitingForConnection || bleSemaphore.waitingForResponse;
}

bool BLEManager::isTimedOut() {
    if((bleSemaphore.startTime + BT_TIMEOUT_MS) < millis() && 
       (bleSemaphore.waitingForConnection || bleSemaphore.waitingForResponse)) {
        if(bleSemaphore.waitingForConnection && bleSemaphore.btDevice) {
            logger.log(ERROR, "Timed out waiting for connection to %s", bleSemaphore.btDevice->getPerifpheryName());
        }
        return true;
    }
    return false;
}

/*
 * poll() - Main loop handler, call this from Arduino loop()
 * 
 * Responsibilities:
 * 1. Process pending connections (doConnect flag set by onScanResult callback)
 * 2. Handle BLE response timeouts
 * 3. Run periodic health check (checkForDisconnectedDevices)
 * 
 * CONNECTION RETRY LOGIC (Scenario A - device found but connection fails):
 * - Each failed connectToServer() increments deviceRetryCount
 * - After BLE_MAX_RETRIES (5), enters 30-minute backoff
 * - deviceLastAttempt is recorded for throttling (prevents rapid retries)
 */
void BLEManager::poll() {
    // ========================================================================
    // STEP 1: Process pending connection
    // doConnect is set by onScanResult() when a target device is found.
    // We can't connect from callback context, so we use this flag pattern.
    // ========================================================================
    if(doConnect) {
        doConnect = false;
        int connectingDeviceIndex = targetDeviceIndex;  // Save - connectToServer clears targetDeviceIndex
        
        // Record attempt time - used for:
        // 1. Throttling in onScanResult (BLE_CONNECT_THROTTLE_MS)
        // 2. Determining if scanner is finding device in checkForDisconnectedDevices
        if(connectingDeviceIndex >= 0 && connectingDeviceIndex < deviceCount) {
            deviceLastAttempt[connectingDeviceIndex] = millis();
        }
        
        if(!connectToServer()) {
            // CONNECTION FAILED - increment retry count
            // This is the primary retry path when scanner finds device but can't connect
            if(connectingDeviceIndex >= 0 && connectingDeviceIndex < deviceCount) {
                deviceRetryCount[connectingDeviceIndex]++;
                
                if(deviceRetryCount[connectingDeviceIndex] >= BLE_MAX_RETRIES) {
                    // Too many failures - enter backoff mode
                    // Device will be ignored by onScanResult until backoff expires
                    deviceBackoffUntil[connectingDeviceIndex] = millis() + BLE_BACKOFF_TIME;
                    logger.log(ERROR, "BLE: Device %s failed after %d attempts, backing off for 30 min", 
                        devices[connectingDeviceIndex]->getPerifpheryName(), deviceRetryCount[connectingDeviceIndex]);
                } else {
                    logger.log(WARNING, "BLE: Connection failed for %s, attempt %d/%d", 
                        devices[connectingDeviceIndex]->getPerifpheryName(), 
                        deviceRetryCount[connectingDeviceIndex], BLE_MAX_RETRIES);
                }
            }
        } else {
            // CONNECTION SUCCEEDED - reset all retry tracking for this device
            if(connectingDeviceIndex >= 0 && connectingDeviceIndex < deviceCount) {
                deviceRetryCount[connectingDeviceIndex] = 0;
                deviceBackoffUntil[connectingDeviceIndex] = 0;
                // Note: deviceLastAttempt stays set - that's fine, it's just for throttling
            }
        }
        targetDeviceIndex = -1;  // Clear after processing
        
        // Resume scanning to find other devices
        if(scanningEnabled && !pBLEScan->isScanning()) {
            NimBLEDevice::getScan()->start(0, false, true);
        }
    }
    
    // ========================================================================
    // STEP 2: Handle BLE response timeouts
    // If we're waiting for a response and it's been too long, give up
    // ========================================================================
    if(isWaiting() && isTimedOut()) {
        logger.log(WARNING, "BLE response timed out, clearing semaphore and moving on");
        bleSemaphore.waitingForConnection = false;
        bleSemaphore.waitingForResponse = false;
        bleSemaphore.btDevice = nullptr;
        bleSemaphore.startTime = 0;
    }
    
    // ========================================================================
    // STEP 3: Periodic health check (every BLE_IS_ALIVE_TIME = 30 seconds)
    // Detects: stale devices (connected but no data), devices not being found
    // ========================================================================
    if(millis() > lastBleIsAliveTime + BLE_IS_ALIVE_TIME) {
        checkForDisconnectedDevices();
        lastBleIsAliveTime = millis();
    }
}

/*
 * checkForDisconnectedDevices() - Periodic health check (runs every 30 seconds)
 * 
 * Handles three device states:
 * 1. OFFLINE + IN BACKOFF: Skip - we're intentionally not trying
 * 2. OFFLINE + NOT FOUND BY SCANNER: Increment retry count (Scenario B)
 * 3. STALE (connected but no data): Disconnect and let reconnection happen
 * 4. CONNECTED + CURRENT: All good, reset retry count
 * 
 * CRITICAL: Coordinates with poll() to avoid double-counting retries!
 * - If scanner IS finding device (deviceLastAttempt recent), poll() handles retries
 * - If scanner is NOT finding device, this function handles retries
 * - pollHandlingDevice flag ensures only one path increments
 */
void BLEManager::checkForDisconnectedDevices() {
    bool allConnectedOrBackoff = true;  // Track if we need to keep scanning
    bool anyStale = false;               // Track if any devices have stale data
    
    for(int i = 0; i < deviceCount; i++) {
        // ====================================================================
        // CASE 1: Device is OFFLINE (not connected)
        // ====================================================================
        if(!devices[i]->isConnected()) {
            
            // --- Check if device is in backoff mode (expected to be offline) ---
            if(deviceBackoffUntil[i] > 0 && millis() < deviceBackoffUntil[i]) {
                // In backoff - this device intentionally ignored, don't count as "needs attention"
                continue;
            }
            
            // --- Check if backoff just expired ---
            if(deviceBackoffUntil[i] > 0 && millis() >= deviceBackoffUntil[i]) {
                // Backoff expired! Reset all counters and try again
                deviceBackoffUntil[i] = 0;
                deviceRetryCount[i] = 0;
                deviceLastAttempt[i] = 0;  // Allow immediate connection attempt
                logger.log(INFO, "BLE: Backoff expired for %s, will retry", devices[i]->getPerifpheryName());
            }
            
            // --- Determine WHO should handle retry counting ---
            // scannerFindingDevice: Has there been a connection attempt in last 60 seconds?
            //   If yes, scanner is finding the device and poll() is handling retries
            // pollHandlingDevice: Is poll() already tracking failures for this device?
            //   If retryCount > 0 and we have an attempt timestamp, poll() owns it
            bool scannerFindingDevice = (deviceLastAttempt[i] > 0) && 
                                        (millis() - deviceLastAttempt[i] < BLE_IS_ALIVE_TIME * 2);
            bool pollHandlingDevice = (deviceRetryCount[i] > 0) && 
                                      (deviceLastAttempt[i] > 0);
            
            if(!scannerFindingDevice && !pollHandlingDevice) {
                // ============================================================
                // SCENARIO B: Device not being found by scanner at all
                // Scanner is running but this device never appears in results
                // This handles: device powered off, out of range, etc.
                // ============================================================
                deviceRetryCount[i]++;
                
                if(deviceRetryCount[i] >= BLE_MAX_RETRIES) {
                    // Too many check cycles without finding device - enter backoff
                    deviceBackoffUntil[i] = millis() + BLE_BACKOFF_TIME;
                    logger.log(ERROR, "BLE: Device %s not found by scanner after %d checks, backing off for 30 min", 
                        devices[i]->getPerifpheryName(), deviceRetryCount[i]);
                } else {
                    logger.log(WARNING, "BLE: Device %s not found by scanner, check %d/%d", 
                        devices[i]->getPerifpheryName(), deviceRetryCount[i], BLE_MAX_RETRIES);
                    allConnectedOrBackoff = false;
                }
            } else {
                // Scanner IS finding device, or poll() is handling retries
                // Don't increment here - poll() will handle it
                allConnectedOrBackoff = false;
            }
        }
        // ====================================================================
        // CASE 2: Device is STALE (connected but not receiving data)
        // This can happen if BLE connection is up but device stopped responding
        // ====================================================================
        else if(!devices[i]->isCurrent()) {
            anyStale = true;
            logger.log(ERROR, "BLE: Device %s is stale (connected but no data), disconnecting...", 
                devices[i]->getPerifpheryName());
            devices[i]->disconnect();  // Force reconnection
            allConnectedOrBackoff = false;
            // Reset retry count - we HAD a connection, so device is reachable
            deviceRetryCount[i] = 0;
        }
        // ====================================================================
        // CASE 3: Device is CONNECTED and CURRENT (healthy)
        // ====================================================================
        else {
            // All good - reset retry count in case it had previous failures
            deviceRetryCount[i] = 0;
        }
    }
    
    // If all connected (or in backoff) and none stale, reset startup timer
    if(allConnectedOrBackoff && !anyStale) {
        bleStartupTime = millis();
        return;
    }
    
    // If not all devices connected, restart BLE scan periodically
    if(millis() - lastBleReconnectTime > BLE_RECONNECT_TIME) {
        logger.log(WARNING, "Not all BLE devices connected, restarting scan...");
        scanningEnabled = true;
        pBLEScan->stop();
        delay(100);
        pBLEScan->start(0, false, false);
        lastBleReconnectTime = millis();
    }
}

/*
 * onScanResult() - Callback when BLE scanner finds a device
 * 
 * Called from NimBLE scan context (can't do heavy operations here).
 * If this is one of our target devices, sets doConnect=true for poll() to handle.
 * 
 * FILTERING LOGIC (in order):
 * 1. Skip if already connected
 * 2. Skip if in backoff mode (too many recent failures)
 * 3. Skip if throttled (last attempt was < 10 seconds ago)
 * 4. If passes all checks, mark for connection
 */
void BLEManager::onScanResult(const NimBLEAdvertisedDevice* advertisedDevice) {
    Serial.printf("BLE: Found device: %s\n", advertisedDevice->getName().c_str());
    
    std::string name = advertisedDevice->getName();
    BTDevice* matched = nullptr;
    int matchedIndex = -1;
    
    // Check if this is one of our registered target devices
    for(int i = 0; i < deviceCount; i++) {
        if(!devices[i]->isConnected() && name == devices[i]->getPerifpheryName()) {
            
            // --- FILTER 1: Backoff check ---
            // If we've failed too many times, ignore this device for a while
            if(deviceBackoffUntil[i] > 0 && millis() < deviceBackoffUntil[i]) {
                Serial.printf("BLE: Device %s in backoff, skipping\n", name.c_str());
                continue;
            }
            // Backoff expired - clear it and allow retry
            if(deviceBackoffUntil[i] > 0 && millis() >= deviceBackoffUntil[i]) {
                deviceBackoffUntil[i] = 0;
                deviceRetryCount[i] = 0;
                logger.log(INFO, "BLE: Backoff expired for %s, retrying", name.c_str());
            }
            
            // --- FILTER 2: Throttle check ---
            // Don't retry too quickly after a failed connection attempt
            // This prevents rapid-fire connection loops when device is flaky
            if(deviceLastAttempt[i] > 0 && (millis() - deviceLastAttempt[i]) < BLE_CONNECT_THROTTLE_MS) {
                Serial.printf("BLE: Device %s throttled, skipping\n", name.c_str());
                continue;
            }
            
            // Passed all filters - this device is a valid connection target
            matched = devices[i];
            matchedIndex = i;
            break;
        }
    }
    
    if(matched != nullptr) {
        Serial.printf("BLE: Target device found! Stopping scan.\n");
        NimBLEDevice::getScan()->stop();
        advAddress = advertisedDevice->getAddress();
        advName = advertisedDevice->getName();
        haveAdvDevice = true;
        targetDevice = matched;
        targetDeviceIndex = matchedIndex;
        doConnect = true;
    }
}

void BLEManager::onScanEnd(int reason) {
    Serial.printf("BLE: Scan ended, reason=%d\n", reason);
    if(!doConnect && scanningEnabled) {
        NimBLEDevice::getScan()->start(0, false, true);
    }
}

void BLEManager::onClientConnect(NimBLEClient* pClient) {
    // Connection handling done in handleConnection()
}

void BLEManager::onClientDisconnect(NimBLEClient* pClient, int reason) {
    bleSemaphore.waitingForConnection = false;
    bleSemaphore.waitingForResponse = false;
    bleSemaphore.btDevice = nullptr;
    
    // Determine which device disconnected
    NimBLEAddress addr = pClient->getPeerAddress();
    const uint8_t* addrVal = addr.getVal();
    uint8_t addrBytes[6];
    memcpy(addrBytes, addrVal, 6);
    
    for(int i = 0; i < deviceCount; i++) {
        if(memcmp(addrBytes, devices[i]->getPeripheryAddress(), 6) == 0) {
            if(i == 0) renogyCmdSequence = 0;  // Reset Renogy command sequence for BT2
            devices[i]->disconnectCallback(pClient);
            break;
        }
    }
}

/*
 * connectToServer() - Establish BLE connection to a discovered device
 * 
 * Called from poll() when doConnect flag is set by onScanResult().
 * Uses NimBLE client pooling to reuse existing client connections where possible.
 * 
 * CONNECTION STRATEGY:
 * 1. First, try to get existing client for this address (previous connection)
 * 2. If no existing client, try to get any disconnected client to reuse
 * 3. If no reusable clients, create a new client
 * 
 * CLIENT PARAMETERS:
 * - Connection interval: 24-48 (30-60ms in 1.25ms units)
 * - Latency: 0 (respond to every connection event)
 * - Supervision timeout: 400 (4 seconds)
 * - Connect timeout: 5000ms
 * 
 * RETURNS: true if connection initiated successfully, false on failure
 * NOTE: Actual connection success is determined in handleConnection()
 */
bool BLEManager::connectToServer() {
    if(!haveAdvDevice || targetDevice == nullptr) {
        logger.log(ERROR, "BLE: No device to connect to!");
        return false;
    }

    // Copy locally, then clear the shared scan result
    NimBLEAddress connectAddress = advAddress;
    std::string connectName = advName;
    haveAdvDevice = false;
    
    logger.log(INFO, "BLE: Connecting to %s (%s)...", connectName.c_str(), connectAddress.toString().c_str());
    
    NimBLEClient* pClient = nullptr;
    
    // Check if we have a client we should reuse
    if(NimBLEDevice::getCreatedClientCount()) {
        pClient = NimBLEDevice::getClientByPeerAddress(connectAddress);
        if(pClient) {
            // Small delay before reconnect to let the device settle
            delay(500);
            if(!pClient->connect(connectAddress, false)) {
                logger.log(ERROR, "BLE: Reconnect failed");
                return false;
            }
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
        
        pClient->setClientCallbacks(&clientCallbacks, false);
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
    
    // Set up semaphore
    bleSemaphore.btDevice = targetDevice;
    bleSemaphore.waitingForConnection = true;
    bleSemaphore.waitingForResponse = false;
    bleSemaphore.startTime = millis();
    
    // Copy address to device
    const uint8_t* addrVal = connectAddress.getVal();
    memcpy(targetDevice->getPeripheryAddress(), addrVal, 6);
    
    // Handle the connection
    handleConnection(pClient, connectAddress);
    
    return true;
}

/*
 * handleConnection() - Complete BLE connection setup after successful connect
 * 
 * Performs GATT service/characteristic discovery and notification subscription.
 * This is called after pClient->connect() succeeds.
 * 
 * DEVICE-SPECIFIC SETUP:
 * 
 * BT2 (Renogy charger) - Uses two services:
 *   - TX Service (ffd0): Write commands to ffd1 characteristic
 *   - RX Service (fff0): Subscribe to fff1 for responses
 *   - Requires CCCD (0x2902) descriptor for notifications
 * 
 * SOK Battery - Uses single service:
 *   - Service (ffe0): TX=ffe2 (write), RX=ffe1 (subscribe)
 *   - Each battery gets its own callback (sok1NotifyCallback, sok2NotifyCallback)
 * 
 * CRITICAL STEPS:
 * 1. Discover all GATT attributes (services, characteristics, descriptors)
 * 2. Get TX characteristic for sending commands
 * 3. Get RX characteristic for receiving responses
 * 4. Verify CCCD descriptor exists (needed for notifications)
 * 5. Subscribe to RX characteristic with device-specific callback
 * 6. Store characteristic pointers in device reader
 * 7. Update BLE indicator color (yellow=partial, blue=all connected)
 */
void BLEManager::handleConnection(NimBLEClient* pClient, NimBLEAddress address) {
    const uint8_t* addrVal = address.getVal();
    uint8_t addr[6];
    memcpy(addr, addrVal, 6);
    
    // Discover all attributes including CCCD descriptors
    if(!pClient->discoverAttributes()) {
        logger.log(ERROR, "BLE: discoverAttributes() failed!");
        pClient->disconnect();
        return;
    }
    
    // Find which device this is and handle accordingly
    int deviceIndex = -1;
    for(int i = 0; i < deviceCount; i++) {
        if(memcmp(addr, devices[i]->getPeripheryAddress(), 6) == 0) {
            deviceIndex = i;
            break;
        }
    }
    
    if(deviceIndex < 0) {
        logger.log(ERROR, "BLE: Unknown device connected");
        pClient->disconnect();
        return;
    }
    
    BTDevice* device = devices[deviceIndex];
    
    // Determine device type by checking its name pattern
    const char* name = device->getPerifpheryName();
    bool isBT2 = (strncmp(name, "BT-TH", 5) == 0);
    bool isSOK = (strncmp(name, "SOK", 3) == 0);
    
    if(isBT2) {
        // BT2 Reader - Renogy device
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
        
        // Verify CCCD descriptor exists
        NimBLERemoteDescriptor* pCccd = pRxChar->getDescriptor(NimBLEUUID((uint16_t)0x2902));
        if(!pCccd) {
            logger.log(WARNING, "BLE: BT2 CCCD (0x2902) NOT FOUND - notifications may not work!");
        }
        
        // Subscribe for notifications
        bool subscribed = false;
        if(pRxChar->canNotify()) {
            subscribed = pRxChar->subscribe(true, bt2NotifyCallback);
        } else if(pRxChar->canIndicate()) {
            subscribed = pRxChar->subscribe(false, bt2NotifyCallback);
        } else {
            logger.log(ERROR, "BLE: BT2 RX char has no notify/indicate!");
        }
        if(!subscribed) {
            logger.log(ERROR, "BLE: BT2 subscribe FAILED");
            pClient->disconnect();
            return;
        }
        delay(100);
        
        device->setCharacteristics(pClient, pTxChar, pRxChar);
        device->resetStale();
        bleSemaphore.waitingForConnection = false;
    }
    else if(isSOK) {
        // SOK Battery
        NimBLERemoteService* pSvc = pClient->getService("ffe0");
        
        if(!pSvc) {
            logger.log(ERROR, "BLE: SOK service ffe0 not found");
            pClient->disconnect();
            return;
        }
        
        NimBLERemoteCharacteristic* pRxChar = pSvc->getCharacteristic("ffe1");
        NimBLERemoteCharacteristic* pTxChar = pSvc->getCharacteristic("ffe2");
        
        if(!pRxChar || !pTxChar) {
            logger.log(ERROR, "BLE: SOK chars not found rx=%p tx=%p", pRxChar, pTxChar);
            pClient->disconnect();
            return;
        }
        
        // Verify CCCD descriptor exists
        NimBLERemoteDescriptor* pCccd = pRxChar->getDescriptor(NimBLEUUID((uint16_t)0x2902));
        if(!pCccd) {
            logger.log(WARNING, "BLE: SOK CCCD (0x2902) NOT FOUND - notifications may not work!");
        }
        
        // Subscribe - use appropriate callback based on device index
        bool subscribed = false;
        auto callback = (deviceIndex == 1) ? sok1NotifyCallback : sok2NotifyCallback;
        
        if(pRxChar->canNotify()) {
            subscribed = pRxChar->subscribe(true, callback);
        } else if(pRxChar->canIndicate()) {
            subscribed = pRxChar->subscribe(false, callback);
        } else {
            logger.log(ERROR, "BLE: SOK RX char has no notify/indicate!");
        }
        if(!subscribed) {
            logger.log(ERROR, "BLE: SOK subscribe FAILED");
            pClient->disconnect();
            return;
        }
        
        bleSemaphore.waitingForConnection = false;
        device->setCharacteristics(pClient, pTxChar, pRxChar);
        device->resetStale();
    }
    
    // Update BLE indicator
    if(indicatorCallback) indicatorCallback(0xFFFF00);  // Yellow
    
    // Check if all devices connected
    if(allDevicesConnected()) {
        if(indicatorCallback) indicatorCallback(0x0000FF);  // Blue
        logger.log(INFO, "All devices connected. Showing online and stopping scan");
        scanningEnabled = false;
        pBLEScan->stop();
    }
    else if(scanningEnabled) {
        pBLEScan->start(0, false, false);
    }
}

// Static notify callbacks
void BLEManager::bt2NotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    Serial.printf(">>> BT2 NOTIFY: len=%d\n", length);
    if(instance && instance->deviceCount > 0) {
        instance->devices[0]->notifyCallback(pChar, pData, length, &instance->bleSemaphore);
    }
}

void BLEManager::sok1NotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    Serial.printf(">>> SOK1 NOTIFY: len=%d\n", length);
    if(instance && instance->deviceCount > 1) {
        instance->devices[1]->notifyCallback(pChar, pData, length, &instance->bleSemaphore);
    }
}

void BLEManager::sok2NotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    Serial.printf(">>> SOK2 NOTIFY: len=%d\n", length);
    if(instance && instance->deviceCount > 2) {
        instance->devices[2]->notifyCallback(pChar, pData, length, &instance->bleSemaphore);
    }
}

// Callback class implementations
void BLEMgrScanCallbacks::onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
    bleManager->onScanResult(advertisedDevice);
}

void BLEMgrScanCallbacks::onScanEnd(const NimBLEScanResults& results, int reason) {
    bleManager->onScanEnd(reason);
}

void BLEMgrClientCallbacks::onConnect(NimBLEClient* pClient) {
    bleManager->onClientConnect(pClient);
}

void BLEMgrClientCallbacks::onDisconnect(NimBLEClient* pClient, int reason) {
    bleManager->onClientDisconnect(pClient, reason);
}
