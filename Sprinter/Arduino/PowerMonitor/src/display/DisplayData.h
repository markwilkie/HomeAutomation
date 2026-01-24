#ifndef DISPLAYDATA_H
#define DISPLAYDATA_H


enum BatteryDisplayMode {
    BATTERY_COMBINED = 0,
    BATTERY_SOK1 = 1,
    BATTERY_SOK2 = 2
};

// Device connection/data status for color coding
enum DeviceStatus {
    DEVICE_ONLINE = 0,      // Connected and receiving current data (normal color)
    DEVICE_STALE = 1,       // Connected but data is stale (grey - keep last value)
    DEVICE_OFFLINE = 2      // Not connected (red - zero values)
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
    float batteryHoursRem;
    float batteryHoursRem2;
    float waterDaysRem;
    float gasDaysRem;
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