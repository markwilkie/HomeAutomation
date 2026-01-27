#include "ScreenController.h"
#include "../tanks/WaterTank.h"
#include "../tanks/GasTank.h"

// Static instance for callbacks
ScreenController* ScreenController::instance = nullptr;

ScreenController::ScreenController() {
    instance = this;
}

void ScreenController::init(Layout* _layout, Screen* _screen, BLEManager* _bleManager) {
    layout = _layout;
    screen = _screen;
    bleManager = _bleManager;
    
    // Register touch callbacks
    screen->addTouchCallback(&ScreenController::touchCallback);
    screen->addLongTouchCallback(&ScreenController::longTouchCallback);
    
    // Initialize timing
    lastBitmapUpdateTime = millis();
}

void ScreenController::setDeviceReaders(BT2Reader* bt2, SOKReader* sok1, SOKReader* sok2) {
    bt2Reader = bt2;
    sokReader1 = sok1;
    sokReader2 = sok2;
}

void ScreenController::setTankReaders(WaterTank* water, GasTank* gas) {
    waterTank = water;
    gasTank = gas;
}

/*
 * update() - Main screen update handler, called from Arduino loop()
 * 
 * Manages display refresh timing and coordinates data loading.
 * 
 * TWO UPDATE CYCLES:
 * 1. SCR_UPDATE_TIME (500ms) - Text and meter values
 *    - Load current values from BLE readers
 *    - Update LCD text displays
 *    - Update WiFi/BLE indicators
 *    - Track water/gas usage
 *    - Calculate data rate (hertz)
 * 
 * 2. BITMAP_UPDATE_TIME (longer) - Bar meter fills
 *    - More expensive redraw operation
 *    - Updates battery/water/gas bar fills
 * 
 * PARAMETERS:
 * @param rtc - Real-time clock for timestamp display
 * @param wifiConnected - Current WiFi connection status for indicator
 * 
 * The delay(50) calls after screen updates help avoid
 * power spikes that could affect analog readings.
 */
void ScreenController::update(ESP32Time* rtc, bool wifiConnected) {
    // Screen update
    if(millis() > lastScrUpdateTime + SCR_UPDATE_TIME) {
        lastScrUpdateTime = millis();
        
        // loadSimulatedValues();  // Uncomment for testing
        loadValues();
        
        if(currentScreen == MAIN_SCREEN) {
            layout->updateLCD(rtc);
        }
        layout->setWifiIndicator(wifiConnected);
        if(bleManager) {
            layout->setBLEIndicator(bleManager->getCurrentIndicatorColor());
        }
        
        // Track water usage percentage per day
        if(waterTank) waterTank->updateUsage();
        // Track gas usage percentage per day
        if(gasTank) gasTank->updateUsage();
        
        // Calc hertz
        if(millis() > hertzTime + 1000) {
            layout->displayData.currentHertz = hertzCount;
            hertzTime = millis();
            hertzCount = 0;
        }
        
        // Delay after screen refresh to avoid power spike
        delay(50);
    }
    
    // Update bitmaps if needed
    if(millis() > lastBitmapUpdateTime + BITMAP_UPDATE_TIME) {
        if(currentScreen == MAIN_SCREEN) {
            layout->updateBitmaps();
        }
        
        // Delay after screen refresh to avoid power spike
        delay(100);
        
        lastBitmapUpdateTime = millis();
    }
}

// Static callback wrapper
void ScreenController::touchCallback(int x, int y) {
    if(instance) {
        instance->handleTouch(x, y);
    }
}

// Static callback wrapper
void ScreenController::longTouchCallback(int x, int y) {
    if(instance) {
        instance->handleLongTouch(x, y);
    }
}

/*
 * handleTouch() - Process short touch events
 * 
 * Called via static callback when finger lift is detected (not long press).
 * Any touch resets screen brightness to full.
 * 
 * TOUCH REGIONS AND ACTIONS:
 * 
 * VAN REGION (bottom right, van/sun icons):
 * - From MAIN_SCREEN: Switch to BT2_DETAIL_SCREEN
 * - From BT2_DETAIL_SCREEN: Return to MAIN_SCREEN
 * 
 * BATTERY ICON REGION (bottom left, battery icon):
 * - Cycles battery display mode: COMBINED -> SOK1 -> SOK2 -> COMBINED
 * - Affects which battery's amps/temp/CMOS/DMOS are shown
 * 
 * Note: Long touches are handled separately in handleLongTouch().
 */
void ScreenController::handleTouch(int x, int y) {
    // Any touch increases screen brightness
    screen->setBrightness(STND_BRIGHTNESS);
    
    // Check if van region touched - toggle between main and BT2 detail screens
    if(currentScreen == MAIN_SCREEN && layout->isVanRegion(x, y)) {
        currentScreen = BT2_DETAIL_SCREEN;
        layout->showBT2Detail(bt2Reader);
        logger.log(INFO, "BT2 detail screen shown");
    }
    else if(currentScreen == BT2_DETAIL_SCREEN) {
        currentScreen = MAIN_SCREEN;
        layout->drawInitialScreen();
        // Restore correct BLE indicator color after redraw
        if(bleManager) {
            layout->setBLEIndicator(bleManager->getCurrentIndicatorColor());
        }
        logger.log(INFO, "Returned to main screen");
    }
    // Check if battery icon touched - cycle through modes
    else if(currentScreen == MAIN_SCREEN && layout->isBatteryIconRegion(x, y)) {
        // Cycle: COMBINED -> SOK1 -> SOK2 -> COMBINED
        if(layout->displayData.batteryMode == BATTERY_COMBINED)
            layout->displayData.batteryMode = BATTERY_SOK1;
        else if(layout->displayData.batteryMode == BATTERY_SOK1)
            layout->displayData.batteryMode = BATTERY_SOK2;
        else
            layout->displayData.batteryMode = BATTERY_COMBINED;
        
        logger.log(INFO, "Battery mode: %d", layout->displayData.batteryMode);
    }
}

/*
 * handleLongTouch() - Process long press events
 * 
 * Called when touch duration exceeds LONG_TOUCH_TIME.
 * Used for less common maintenance actions.
 * 
 * LONG TOUCH REGIONS AND ACTIONS:
 * 
 * CENTER REGION (middle of screen):
 * - Performs full display reset (lcd.init())
 * - Useful for brownout recovery when display is garbled
 * - Redraws entire initial screen
 * 
 * BLE REGION (top right corner):
 * - Toggles BLE on/off
 * - Calls bleManager->turnOff() or turnOn()
 * - Useful for power saving or debugging BLE issues
 */
void ScreenController::handleLongTouch(int x, int y) {
    // Long touch in center region - reset screen (for brownout recovery)
    if(layout->isCenterRegion(x, y)) {
        logger.log(WARNING, "Screen reset requested via long touch");
        screen->resetDisplay();
        layout->drawInitialScreen();
        // Restore correct BLE indicator color after reset
        if(bleManager) {
            layout->setBLEIndicator(bleManager->getCurrentIndicatorColor());
        }
    }
    // Check if in the BLE region - toggle BLE on/off
    else if(layout->isBLERegion(x, y)) {
        static bool BLEOn = true;
        BLEOn = !BLEOn;
        logger.log(WARNING, "BLE Toggled %d (%d,%d)", BLEOn, x, y);
        if(!BLEOn) {
            bleManager->turnOff();
        }
        else {
            bleManager->turnOn();
        }
    }
}

/*
 * loadValues() - Aggregate data from all BLE readers into displayData
 * 
 * Copies current values from device readers to layout.displayData struct.
 * Handles device status (online/stale/offline) to determine display behavior.
 * 
 * DEVICE STATUS LOGIC:
 * - DEVICE_ONLINE: Connected AND receiving current data
 *   -> Update displayData with latest values
 * - DEVICE_STALE: Connected but no recent data, OR disconnected but still trying
 *   -> Keep previous values (don't update)
 * - DEVICE_OFFLINE: Disconnected AND in backoff (gave up trying)
 *   -> Zero out values
 * 
 * BATTERY MODE HANDLING:
 * The batteryMode setting (COMBINED/SOK1/SOK2) determines which battery's
 * detailed data (amps, temp, CMOS, DMOS, heater) is shown in the battery icon area.
 * 
 * DERIVED VALUES:
 * - chargeAmps: Solar + Alternator from BT2
 * - drawAmps: chargeAmps minus net battery current (shows house load)
 * - currentVolts: Average of both battery voltages
 * - batteryHoursRem: Capacity ÷ discharge rate
 */
void ScreenController::loadValues() {
    if(!sokReader1 || !sokReader2 || !bt2Reader) return;
    
    // Determine device status for each reader
    // Offline (red, zero values) only if in backoff
    // Stale (grey, keep last value) if disconnected but still trying, or connected but no recent data
    // Online (normal) if connected and current
    
    // SOK1 status - device index 1 (BT2 is 0, SOK1 is 1, SOK2 is 2)
    if(!sokReader1->isConnected()) {
        // Only show as offline (red) if device is in backoff
        if(bleManager && bleManager->isDeviceInBackoff(1)) {
            layout->displayData.sok1Status = DEVICE_OFFLINE;
        } else {
            layout->displayData.sok1Status = DEVICE_STALE;  // Still trying to connect
        }
    } else if(!sokReader1->isCurrent()) {
        layout->displayData.sok1Status = DEVICE_STALE;
    } else {
        layout->displayData.sok1Status = DEVICE_ONLINE;
    }
    
    // SOK2 status - device index 2
    if(!sokReader2->isConnected()) {
        if(bleManager && bleManager->isDeviceInBackoff(2)) {
            layout->displayData.sok2Status = DEVICE_OFFLINE;
        } else {
            layout->displayData.sok2Status = DEVICE_STALE;
        }
    } else if(!sokReader2->isCurrent()) {
        layout->displayData.sok2Status = DEVICE_STALE;
    } else {
        layout->displayData.sok2Status = DEVICE_ONLINE;
    }
    
    // BT2 status - device index 0
    if(!bt2Reader->isConnected()) {
        if(bleManager && bleManager->isDeviceInBackoff(0)) {
            layout->displayData.bt2Status = DEVICE_OFFLINE;
        } else {
            layout->displayData.bt2Status = DEVICE_STALE;
        }
    } else if(!bt2Reader->isCurrent()) {
        layout->displayData.bt2Status = DEVICE_STALE;
    } else {
        layout->displayData.bt2Status = DEVICE_ONLINE;
    }
    
    // SOK1 values - zero if offline, keep last if stale
    if(layout->displayData.sok1Status == DEVICE_OFFLINE) {
        layout->displayData.stateOfCharge = 0;
        layout->displayData.batteryVolts = 0;
        layout->displayData.batteryHoursRem = 0;
    } else if(layout->displayData.sok1Status == DEVICE_ONLINE) {
        layout->displayData.stateOfCharge = sokReader1->getSoc();
        layout->displayData.batteryVolts = sokReader1->getVolts();
        if(sokReader1->getAmps() < 0) {
            float hours = sokReader1->getCapacity() / (sokReader1->getAmps() * -1);
            hours = roundf(hours * 10) / 10;  // Round to 1 decimal
            layout->displayData.batteryHoursRem = (hours > 99) ? 0 : hours;
        } else {
            layout->displayData.batteryHoursRem = 0;
        }
    }
    // Stale: don't update, keep last values
    
    // SOK2 values - zero if offline, keep last if stale
    if(layout->displayData.sok2Status == DEVICE_OFFLINE) {
        layout->displayData.stateOfCharge2 = 0;
        layout->displayData.batteryVolts2 = 0;
        layout->displayData.batteryHoursRem2 = 0;
    } else if(layout->displayData.sok2Status == DEVICE_ONLINE) {
        layout->displayData.stateOfCharge2 = sokReader2->getSoc();
        layout->displayData.batteryVolts2 = sokReader2->getVolts();
        if(sokReader2->getAmps() < 0) {
            float hours = sokReader2->getCapacity() / (sokReader2->getAmps() * -1);
            hours = roundf(hours * 10) / 10;  // Round to 1 decimal
            layout->displayData.batteryHoursRem2 = (hours > 99) ? 0 : hours;
        } else {
            layout->displayData.batteryHoursRem2 = 0;
        }
    }
    // Stale: don't update, keep last values
    
    // BT2 values - zero if offline, keep last if stale
    if(layout->displayData.bt2Status == DEVICE_OFFLINE) {
        layout->displayData.chargeAmps = 0;
        layout->displayData.chargerTemperature = 0;
        layout->displayData.solarAmpValue = 0;
        layout->displayData.alternaterAmpValue = 0;
    } else if(layout->displayData.bt2Status == DEVICE_ONLINE) {
        layout->displayData.chargeAmps = bt2Reader->getSolarAmps() + bt2Reader->getAlternaterAmps();
        layout->displayData.chargerTemperature = bt2Reader->getTemperature();
        layout->displayData.solarAmpValue = bt2Reader->getSolarAmps();
        layout->displayData.alternaterAmpValue = bt2Reader->getAlternaterAmps();
    }
    // Stale: don't update, keep last values
    
    // Combined/derived values - only update if both SOKs are online
    if(layout->displayData.sok1Status == DEVICE_ONLINE && layout->displayData.sok2Status == DEVICE_ONLINE) {
        float totalAmps = sokReader1->getAmps() + sokReader2->getAmps();
        layout->displayData.currentVolts = (sokReader1->getVolts() + sokReader2->getVolts()) / 2.0;
        
        // Calculate actual house load based on battery state
        if(totalAmps < 0)
            layout->displayData.drawAmps = layout->displayData.chargeAmps + (-totalAmps);
        else
            layout->displayData.drawAmps = layout->displayData.chargeAmps - totalAmps;
    } else if(layout->displayData.sok1Status == DEVICE_OFFLINE && layout->displayData.sok2Status == DEVICE_OFFLINE) {
        layout->displayData.currentVolts = 0;
        layout->displayData.drawAmps = 0;
    }
    // If one is stale or mixed, keep last values

    // Use cached water and gas tank values (show '-' if > 9 days)
    // Round to 1 decimal place to match display format
    if(waterTank) {
        layout->displayData.stateOfWater = waterTank->getWaterLevel();
        float waterDays = waterTank->getWaterDaysRemaining();
        waterDays = roundf(waterDays * 10) / 10;  // Round to 1 decimal
        layout->displayData.waterDaysRem = (waterDays > 9) ? 0 : waterDays;
    }
    if(gasTank) {
        layout->displayData.stateOfGas = gasTank->getGasLevel();
        float gasDays = gasTank->getGasDaysRemaining();
        gasDays = roundf(gasDays * 10) / 10;  // Round to 1 decimal
        layout->displayData.gasDaysRem = (gasDays > 9) ? 0 : gasDays;
    }
    
    // Set battery-specific values based on display mode
    if(layout->displayData.batteryMode == BATTERY_COMBINED) {
        // Only update if both online
        if(layout->displayData.sok1Status == DEVICE_ONLINE && layout->displayData.sok2Status == DEVICE_ONLINE) {
            layout->displayData.batteryTemperature = (sokReader1->getTemperature() + sokReader2->getTemperature()) / 2.0;
            layout->displayData.batteryAmpValue = sokReader1->getAmps() + sokReader2->getAmps();
            layout->displayData.cmos = sokReader1->isCMOS() && sokReader2->isCMOS();
            layout->displayData.dmos = sokReader1->isDMOS() && sokReader2->isDMOS();
            layout->displayData.heater = sokReader1->isHeating() || sokReader2->isHeating();
        } else if(layout->displayData.sok1Status == DEVICE_OFFLINE && layout->displayData.sok2Status == DEVICE_OFFLINE) {
            layout->displayData.batteryTemperature = 0;
            layout->displayData.batteryAmpValue = 0;
            layout->displayData.cmos = false;
            layout->displayData.dmos = false;
            layout->displayData.heater = false;
        }
    }
    else if(layout->displayData.batteryMode == BATTERY_SOK1) {
        if(layout->displayData.sok1Status == DEVICE_ONLINE) {
            layout->displayData.batteryTemperature = sokReader1->getTemperature();
            layout->displayData.batteryAmpValue = sokReader1->getAmps();
            layout->displayData.cmos = sokReader1->isCMOS();
            layout->displayData.dmos = sokReader1->isDMOS();
            layout->displayData.heater = sokReader1->isHeating();
        } else if(layout->displayData.sok1Status == DEVICE_OFFLINE) {
            layout->displayData.batteryTemperature = 0;
            layout->displayData.batteryAmpValue = 0;
            layout->displayData.cmos = false;
            layout->displayData.dmos = false;
            layout->displayData.heater = false;
        }
    }
    else { // BATTERY_SOK2
        if(layout->displayData.sok2Status == DEVICE_ONLINE) {
            layout->displayData.batteryTemperature = sokReader2->getTemperature();
            layout->displayData.batteryAmpValue = sokReader2->getAmps();
            layout->displayData.cmos = sokReader2->isCMOS();
            layout->displayData.dmos = sokReader2->isDMOS();
            layout->displayData.heater = sokReader2->isHeating();
        } else if(layout->displayData.sok2Status == DEVICE_OFFLINE) {
            layout->displayData.batteryTemperature = 0;
            layout->displayData.batteryAmpValue = 0;
            layout->displayData.cmos = false;
            layout->displayData.dmos = false;
            layout->displayData.heater = false;
        }
    }
}

void ScreenController::loadSimulatedValues() {
    // Static variables to track water percentage cycling
    static int waterPercent = 0;
    static bool waterIncreasing = true;
    
    if(waterIncreasing) {
        waterPercent += 1;
        if(waterPercent >= 100) waterIncreasing = false;
    }
    else {
        waterPercent -= 1;
        if(waterPercent <= 0) waterIncreasing = true;
    }
    
    layout->displayData.stateOfCharge = random(90) + 10;
    layout->displayData.stateOfCharge2 = random(90) + 10;
    layout->displayData.stateOfWater = waterPercent;
    layout->displayData.stateOfGas = random(100);
    
    layout->displayData.chargeAmps = ((double)random(150)) / 9.2;
    layout->displayData.drawAmps = ((double)random(100)) / 9.2;

    layout->displayData.currentVolts = ((double)random(40) + 100.0) / 10.0;
    layout->displayData.batteryVolts = ((double)random(40) + 100.0) / 10.0;
    layout->displayData.batteryVolts2 = ((double)random(40) + 100.0) / 10.0;
    layout->displayData.batteryHoursRem = ((double)random(99)) / 10.0;
    layout->displayData.batteryHoursRem2 = ((double)random(99)) / 10.0;
    layout->displayData.waterDaysRem = ((double)random(10)) / 10.0;
    layout->displayData.gasDaysRem = ((double)random(10)) / 10.0;

    layout->displayData.batteryTemperature = ((double)random(90)) - 20;
    layout->displayData.chargerTemperature = ((double)random(90)) - 20;
    layout->displayData.batteryAmpValue = ((double)random(200)) / 10.0;
    layout->displayData.solarAmpValue = ((double)random(200)) / 10.0;
    layout->displayData.alternaterAmpValue = ((double)random(200)) / 10.0;

    layout->displayData.heater = random(2);
    layout->displayData.cmos = random(2);
    layout->displayData.dmos = random(2);
}
