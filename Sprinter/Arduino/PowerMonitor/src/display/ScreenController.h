#ifndef SCREEN_CONTROLLER_H
#define SCREEN_CONTROLLER_H

/*
 * ScreenController - Central coordinator for display, touch, and data flow
 * 
 * RESPONSIBILITIES:
 * 1. Screen state management (main screen vs detail screens)
 * 2. Touch event routing (short touch, long touch, region detection)
 * 3. Data flow from device readers to display (loadValues)
 * 4. Update timing (screen refresh rate, bitmap update rate)
 * 5. Hertz counter for BLE data rate monitoring
 * 
 * TOUCH INTERACTIONS:
 * - Short touch on van icon: Toggle to BT2 detail screen
 * - Short touch on BT2 detail screen: Return to main screen
 * - Short touch on battery icon: Cycle battery display mode (combined/SOK1/SOK2)
 * - Long touch on center: Reset display (brownout recovery)
 * - Long touch on BLE icon region: Toggle BLE on/off
 * 
 * DATA FLOW:
 * - Device readers (BT2Reader, SOKReader) -> loadValues() -> DisplayData -> Layout
 * - Tank sensors (WaterTank, GasTank) -> loadValues() -> DisplayData -> Layout
 * 
 * UPDATE CYCLE (called from main loop):
 * 1. update() called every loop iteration
 * 2. If SCR_UPDATE_TIME elapsed: loadValues(), updateLCD(), updateIndicators()
 * 3. If BITMAP_UPDATE_TIME elapsed: updateBitmaps() (bar meters)
 */

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

// ============================================================================
// TIMING CONSTANTS
// ============================================================================
#define SCR_UPDATE_TIME 500      // Update screen values every 500ms
#define BITMAP_UPDATE_TIME 5000  // Update bar meter graphics every 5 seconds

// Screen state management
enum ScreenState {
    MAIN_SCREEN,           // Primary dashboard view
    BT2_DETAIL_SCREEN      // BT2 charge controller detail view
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
