#ifndef WATERTANK_H
#define WATERTANK_H

/*
 * WaterTank - Fresh Water Tank Level Monitoring via Resistive Sender
 * 
 * HARDWARE SETUP:
 * - Standard automotive-style resistive tank sender
 * - 33Ω when full, 240Ω when empty
 * - Powered from 5V rail, read via voltage divider to 3.3V ADC
 * 
 * CIRCUIT:
 *     5V (from external rail)
 *      |
 * [ Tank Sender ]  ← 33Ω (full) to 240Ω (empty)
 *      |
 *      |------- GPIO12 (analog input via voltage divider)
 *      |
 * [ 240Ω Resistor ]  ← fixed pull-down
 *      |
 *     GND
 * 
 * VOLTAGE READINGS:
 * - Full (33Ω):  ~2.90V at ADC
 * - Empty (240Ω): ~1.65V at ADC
 * 
 * USAGE TRACKING:
 * - Hourly weighted average of consumption rate
 * - Days remaining calculated from current level and usage rate
 */

#include <Arduino.h>
#include "../logging/logger.h"

extern Logger logger;

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define WATER_LEVEL_ANALOG_PIN 12    // GPIO12 for water level sensor

#define PUMP_CURRENT_THRESHOLD 2.2   // ACS712 threshold (sensor runs inverted: lower V = higher A)
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
