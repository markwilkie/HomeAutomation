#ifndef WATERTANK_H
#define WATERTANK_H

#include <Arduino.h>
#include "src/logging/logger.h"

extern Logger logger;

//Defines
#define WATER_LEVEL_ANALOG_PIN 12 // water level sensor

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

#endif
