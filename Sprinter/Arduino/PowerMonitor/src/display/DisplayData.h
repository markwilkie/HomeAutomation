#ifndef DISPLAYDATA_H
#define DISPLAYDATA_H

/*
 * DisplayData - Centralized data structure for UI display values
 * 
 * This struct holds all values that are displayed on the screen.
 * It acts as a bridge between device readers and the display layer.
 * 
 * VALUES ARE POPULATED BY:
 * - ScreenController::loadValues() - reads from device readers and populates this struct
 * 
 * VALUES ARE CONSUMED BY:
 * - Layout::updateLCD() - renders values to screen
 * - Layout::updateBitmaps() - updates bar meters and icons
 * 
 * DEVICE STATUS:
 * - ONLINE (white text): Device connected and receiving current data
 * - STALE (grey text): Device disconnected or no recent data, showing last known value
 * - OFFLINE (red text): Device in backoff mode after multiple connection failures
 */

// Battery display mode - which battery's data to show in center meters
enum BatteryDisplayMode {
    BATTERY_COMBINED = 0,   // Show combined/averaged data from both batteries
    BATTERY_SOK1 = 1,       // Show only SOK battery 1 data
    BATTERY_SOK2 = 2        // Show only SOK battery 2 data
};

// Device connection/data status for color coding
// This determines text color in the UI to indicate data freshness
enum DeviceStatus {
    DEVICE_ONLINE = 0,      // Connected and receiving current data (white - normal)
    DEVICE_STALE = 1,       // Connected but data is stale (grey - showing last value)
    DEVICE_OFFLINE = 2      // Not connected / in backoff (red - zero values)
};

struct DisplayData
{
    int stateOfCharge;
    int stateOfCharge2;
    int stateOfWater;
    int stateOfGas;
    int rangeForWater;
    float currentVolts;
    float batteryVolts;
    float batteryVolts2;
    float chargeAmps;
    float drawAmps;
    float batteryHoursRem = 0;
    float batteryHoursRem2 = 0;
    float waterDaysRem = 0;
    float gasDaysRem = 0;
    float batteryTemperature;
    float chargerTemperature;
    float batteryAmpValue;
    float solarAmpValue;
    float alternaterAmpValue;
    bool cmos;
    bool dmos;
    bool heater;
    int currentHertz;
    BatteryDisplayMode batteryMode;
    
    // Device status for color coding
    DeviceStatus sok1Status = DEVICE_OFFLINE;
    DeviceStatus sok2Status = DEVICE_OFFLINE;
    DeviceStatus bt2Status = DEVICE_OFFLINE;
};

#endif