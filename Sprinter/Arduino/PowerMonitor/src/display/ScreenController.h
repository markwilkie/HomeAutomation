#ifndef SCREEN_CONTROLLER_H
#define SCREEN_CONTROLLER_H

#include <ESP32Time.h>
#include "Layout.h"
#include "Screen.h"
#include "../ble/BLEManager.h"
#include "../ble/BT2Reader.h"
#include "../ble/SOKReader.h"
#include "../logging/logger.h"

// Forward declarations
class WaterTank;
class GasTank;

extern Logger logger;

// Screen update timing constants
#define SCR_UPDATE_TIME 500
#define BITMAP_UPDATE_TIME 5000

// Screen state management
enum ScreenState {
    MAIN_SCREEN,
    WATER_DETAIL_SCREEN
};

class ScreenController {
public:
    ScreenController();
    
    // Initialization
    void init(Layout* layout, Screen* screen, BLEManager* bleManager);
    
    // Set device readers for loading values
    void setDeviceReaders(BT2Reader* bt2, SOKReader* sok1, SOKReader* sok2);
    
    // Set tank readers
    void setTankReaders(WaterTank* water, GasTank* gas);
    
    // Main update function - call from loop()
    void update(ESP32Time* rtc, bool wifiConnected);
    
    // Touch callbacks - static wrappers for C-style callbacks
    static void touchCallback(int x, int y);
    static void longTouchCallback(int x, int y);
    
    // Get current screen state
    ScreenState getCurrentScreen() { return currentScreen; }
    
    // Hertz tracking
    void incrementHertzCount() { hertzCount++; }
    int getHertzCount() { return hertzCount; }

private:
    // Load values from device readers into display data
    void loadValues();
    void loadSimulatedValues();
    
    // Touch handlers
    void handleTouch(int x, int y);
    void handleLongTouch(int x, int y);
    
    // References to other objects
    Layout* layout = nullptr;
    Screen* screen = nullptr;
    BLEManager* bleManager = nullptr;
    BT2Reader* bt2Reader = nullptr;
    SOKReader* sokReader1 = nullptr;
    SOKReader* sokReader2 = nullptr;
    WaterTank* waterTank = nullptr;
    GasTank* gasTank = nullptr;
    
    // Screen state
    ScreenState currentScreen = MAIN_SCREEN;
    
    // Timing
    unsigned long lastScrUpdateTime = 0;
    unsigned long lastBitmapUpdateTime = 0;
    unsigned long hertzTime = 0;
    int hertzCount = 0;
    
    // Static instance for callbacks
    static ScreenController* instance;
};

#endif
