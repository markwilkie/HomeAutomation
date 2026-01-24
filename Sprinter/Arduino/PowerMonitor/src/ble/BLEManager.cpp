#include "BLEManager.h"

// Static instance pointer for static callbacks
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

void BLEManager::poll() {
    // Process pending connection - matches official NimBLE example pattern
    if(doConnect) {
        doConnect = false;
        int connectingDeviceIndex = targetDeviceIndex;  // Save before connectToServer clears it
        
        if(!connectToServer()) {
            logger.log(WARNING, "BLE: Connection failed for %s", 
                (connectingDeviceIndex >= 0 && connectingDeviceIndex < deviceCount) 
                    ? devices[connectingDeviceIndex]->getPerifpheryName() : "unknown");
            // Don't reset stack here - let checkForDisconnectedDevices handle retry logic
        } else {
            // Connection succeeded - reset retry count for this device
            if(connectingDeviceIndex >= 0 && connectingDeviceIndex < deviceCount) {
                deviceRetryCount[connectingDeviceIndex] = 0;
                deviceBackoffUntil[connectingDeviceIndex] = 0;
            }
        }
        targetDeviceIndex = -1;  // Clear after processing
        
        // Restart scanning for more devices
        if(scanningEnabled && !pBLEScan->isScanning()) {
            NimBLEDevice::getScan()->start(0, false, true);
        }
    }
    
    // Check for BLE timeouts
    if(isWaiting() && isTimedOut()) {
        logger.log(WARNING, "BLE response timed out, clearing semaphore and moving on");
        bleSemaphore.waitingForConnection = false;
        bleSemaphore.waitingForResponse = false;
        bleSemaphore.btDevice = nullptr;
        bleSemaphore.startTime = 0;
    }
    
    // Check for disconnected/stale devices periodically
    if(millis() > lastBleIsAliveTime + BLE_IS_ALIVE_TIME) {
        checkForDisconnectedDevices();
        lastBleIsAliveTime = millis();
    }
}

void BLEManager::checkForDisconnectedDevices() {
    bool allConnectedOrBackoff = true;
    bool anyStale = false;
    
    for(int i = 0; i < deviceCount; i++) {
        if(!devices[i]->isConnected()) {
            // Device is offline - check if it's in backoff (expected to be offline)
            if(deviceBackoffUntil[i] > 0 && millis() < deviceBackoffUntil[i]) {
                // In backoff, treat as "expected offline" and don't trigger reconnect logic
                continue;
            }
            
            // Backoff expired or never set - this device needs attention
            if(deviceBackoffUntil[i] > 0 && millis() >= deviceBackoffUntil[i]) {
                // Backoff just expired, reset counters
                deviceBackoffUntil[i] = 0;
                deviceRetryCount[i] = 0;
                logger.log(INFO, "BLE: Backoff expired for %s, will retry", devices[i]->getPerifpheryName());
            }
            
            // Increment retry count for not being connected during this check cycle
            deviceRetryCount[i]++;
            
            if(deviceRetryCount[i] >= BLE_MAX_RETRIES) {
                // Enter backoff mode - stop trying for a while
                deviceBackoffUntil[i] = millis() + BLE_BACKOFF_TIME;
                logger.log(ERROR, "BLE: Device %s not found after %d attempts, backing off for 30 min", 
                    devices[i]->getPerifpheryName(), deviceRetryCount[i]);
                // Don't count this as "unexpected offline" since we're handling it
            } else {
                // Still trying to find/connect
                allConnectedOrBackoff = false;
                logger.log(WARNING, "BLE: Device %s not connected, attempt %d/%d", 
                    devices[i]->getPerifpheryName(), deviceRetryCount[i], BLE_MAX_RETRIES);
            }
        }
        else if(!devices[i]->isCurrent()) {
            anyStale = true;
            logger.log(ERROR, "BLE: Device %s is stale (connected but no data), disconnecting...", 
                devices[i]->getPerifpheryName());
            devices[i]->disconnect();
            allConnectedOrBackoff = false;
            // Reset retry count since we had a connection
            deviceRetryCount[i] = 0;
        }
        else {
            // Device is connected and current - reset retry count
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

// Scan callbacks
void BLEManager::onScanResult(const NimBLEAdvertisedDevice* advertisedDevice) {
    Serial.printf("BLE: Found device: %s\n", advertisedDevice->getName().c_str());
    
    std::string name = advertisedDevice->getName();
    BTDevice* matched = nullptr;
    int matchedIndex = -1;
    
    // Check if this is one of our target devices
    for(int i = 0; i < deviceCount; i++) {
        if(!devices[i]->isConnected() && name == devices[i]->getPerifpheryName()) {
            // Check if device is in backoff mode
            if(deviceBackoffUntil[i] > 0 && millis() < deviceBackoffUntil[i]) {
                // Still in backoff, skip this device
                Serial.printf("BLE: Device %s in backoff, skipping\n", name.c_str());
                continue;
            }
            // Backoff expired, clear it
            if(deviceBackoffUntil[i] > 0 && millis() >= deviceBackoffUntil[i]) {
                deviceBackoffUntil[i] = 0;
                deviceRetryCount[i] = 0;
                logger.log(INFO, "BLE: Backoff expired for %s, retrying", name.c_str());
            }
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
