#include "ScreenController.h"
#include "../../WaterTank.h"
#include "../../GasTank.h"

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

void ScreenController::handleTouch(int x, int y) {
    // Check if water region touched - toggle between main and detail screens
    if(currentScreen == MAIN_SCREEN && layout->isWaterRegion(x, y)) {
        currentScreen = WATER_DETAIL_SCREEN;
        layout->showWaterDetail();
        logger.log(INFO, "Water detail screen shown");
    }
    else if(currentScreen == WATER_DETAIL_SCREEN) {
        currentScreen = MAIN_SCREEN;
        layout->drawInitialScreen();
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

void ScreenController::handleLongTouch(int x, int y) {
    // Check if in the BLE region - toggle BLE on/off
    if(layout->isBLERegion(x, y)) {
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

void ScreenController::loadValues() {
    if(!sokReader1 || !sokReader2 || !bt2Reader) return;
    
    // Individual battery data
    layout->displayData.stateOfCharge = sokReader1->getSoc();
    layout->displayData.stateOfCharge2 = sokReader2->getSoc();
    
    // Combined data from both SOK batteries
    float totalAmps = sokReader1->getAmps() + sokReader2->getAmps();
    
    layout->displayData.currentVolts = (sokReader1->getVolts() + sokReader2->getVolts()) / 2.0;
    layout->displayData.batteryVolts = sokReader1->getVolts();
    layout->displayData.batteryVolts2 = sokReader2->getVolts();
    
    // Calculate charge and draw amps for display rings
    layout->displayData.chargeAmps = bt2Reader->getSolarAmps() + bt2Reader->getAlternaterAmps();
    
    // Calculate actual house load based on battery state
    if(totalAmps < 0)
        layout->displayData.drawAmps = layout->displayData.chargeAmps + (-totalAmps);
    else
        layout->displayData.drawAmps = layout->displayData.chargeAmps - totalAmps;
    
    // Calculate hours remaining for each battery
    if(sokReader1->getAmps() < 0)
        layout->displayData.batteryHoursRem = sokReader1->getCapacity() / (sokReader1->getAmps() * -1);
    else
        layout->displayData.batteryHoursRem = 999;
        
    if(sokReader2->getAmps() < 0)
        layout->displayData.batteryHoursRem2 = sokReader2->getCapacity() / (sokReader2->getAmps() * -1);
    else
        layout->displayData.batteryHoursRem2 = 999;

    // Use cached water and gas tank values
    if(waterTank) {
        layout->displayData.stateOfWater = waterTank->getWaterLevel();
        layout->displayData.waterDaysRem = waterTank->getWaterDaysRemaining();
    }
    if(gasTank) {
        layout->displayData.stateOfGas = gasTank->getGasLevel();
        layout->displayData.gasDaysRem = gasTank->getGasDaysRemaining();
    }
    
    // Set battery-specific values based on display mode
    if(layout->displayData.batteryMode == BATTERY_COMBINED) {
        layout->displayData.batteryTemperature = (sokReader1->getTemperature() + sokReader2->getTemperature()) / 2.0;
        layout->displayData.batteryAmpValue = sokReader1->getAmps() + sokReader2->getAmps();
        layout->displayData.cmos = sokReader1->isCMOS() && sokReader2->isCMOS();
        layout->displayData.dmos = sokReader1->isDMOS() && sokReader2->isDMOS();
        layout->displayData.heater = sokReader1->isHeating() || sokReader2->isHeating();
    }
    else if(layout->displayData.batteryMode == BATTERY_SOK1) {
        layout->displayData.batteryTemperature = sokReader1->getTemperature();
        layout->displayData.batteryAmpValue = sokReader1->getAmps();
        layout->displayData.cmos = sokReader1->isCMOS();
        layout->displayData.dmos = sokReader1->isDMOS();
        layout->displayData.heater = sokReader1->isHeating();
    }
    else { // BATTERY_SOK2
        layout->displayData.batteryTemperature = sokReader2->getTemperature();
        layout->displayData.batteryAmpValue = sokReader2->getAmps();
        layout->displayData.cmos = sokReader2->isCMOS();
        layout->displayData.dmos = sokReader2->isDMOS();
        layout->displayData.heater = sokReader2->isHeating();
    }
    
    layout->displayData.chargerTemperature = bt2Reader->getTemperature();
    layout->displayData.solarAmpValue = bt2Reader->getSolarAmps();
    layout->displayData.alternaterAmpValue = bt2Reader->getAlternaterAmps();
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
