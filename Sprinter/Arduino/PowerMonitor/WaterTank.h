#ifndef WATERTANK_H
#define WATERTANK_H

#include <Arduino.h>
#include "src/logging/logger.h"

extern Logger logger;

//Defines
#define WATER_LEVEL_ANALOG_PIN 10 // GPIO10 for water level sensor
#define WATER_PUMP_CURRENT_ANALOG_PIN 11 
#define WATER_PUMP_INDICATOR_LIGHT 12   // not yet implemented

#define PUMP_CURRENT_THRESHOLD 2.2 // Remember that we're running the sensor backwards, so less than is more amps
#define USAGE_CHECK_INTERVAL 3600000UL // Check usage every hour (in milliseconds)

class WaterTank {
private:
  //properties
  long waterRemaining;
  int lastLevel = -1;
  unsigned long lastLevelTime = 0;
  float dailyPercentUsed = 0; // Hourly percent used (weighted average)
  unsigned long lastDayCheck = 0; // Last hourly check time
  
  // Auto-calibration using fill detection
  float fullVoltage = 2.90;      // Expected voltage at full (33Ω)
  float emptyVoltage = 1.65;     // Expected voltage at empty (240Ω)
  float lastVoltage = 0.0;       // Track last voltage for change detection
  unsigned long lastCalCheck = 0; // Timestamp for calibration checks
  bool isCalibrated = false;     // Track if we have valid calibration
  
  // Cached sensor values
  int waterLevel = 0;
  float waterDaysRem = 0;
   
public:
  //members
  void readWaterLevel(); // Reads sensor and updates waterLevel cache
  void updateUsage(); // Updates hourly usage based on current level and time since last update
  void updateDaysRemaining(); // Updates waterDaysRem cache based on current usage
  
  // Getters for cached values
  int getWaterLevel() const { return waterLevel; }
  float getWaterDaysRemaining() const { return waterDaysRem; }
};

// Pump class: determines if the pump is running based on analog input from a current sensor
class WaterPump {
private:
  unsigned long runningStart;
  bool wasRunning;
public:
  void init();
  void updateLight();
  bool isRunning();
};

#endif
