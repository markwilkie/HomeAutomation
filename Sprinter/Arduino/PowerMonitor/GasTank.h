#ifndef GASTANK_H
#define GASTANK_H

/*
 * GasTank - Propane Tank Level Monitoring via Force Sensing Resistor (FSR)
 * 
 * HARDWARE SETUP:
 * - FSR C10 sensor under the propane tank
 * - Sensor measures weight/force which correlates to tank fill level
 * - Connected to GPIO13 via voltage divider (10kΩ pull-down)
 * 
 * MEASUREMENT APPROACH:
 * - FSR has logarithmic R vs force, but linear conductance (1/R) vs force
 * - ADC reading converted to voltage, then to resistance, then to conductance
 * - Conductance converted to force using FSR datasheet linear equation
 * - Force calibrated to percentage based on full/empty tank weights
 * 
 * NOTE: WiFi must be disabled during ADC reads on GPIO13 due to hardware conflict
 * with ESP32-S3 WiFi radio. Tank reads are done in a WiFi-off window.
 * 
 * USAGE TRACKING:
 * - Hourly weighted average of consumption rate
 * - Days remaining calculated from current level and usage rate
 */

#include <Arduino.h>
#include "src/logging/logger.h"

extern Logger logger;

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define GAS_LEVEL_ANALOG_PIN 13      // GPIO13 - Note: WiFi must be OFF during read!
#define GAS_ADC_SAMPLES 40           // Number of samples to average (noise reduction)
#define USAGE_CHECK_INTERVAL 3600000UL // Check usage every hour (in milliseconds)

class GasTank {
private:
  //properties
  long gasRemaining;
  int lastLevel = -1;
  unsigned long lastLevelTime = 0;
  float dailyPercentUsed = 0; // Hourly percent used (weighted average)
  unsigned long lastDayCheck = 0; // Last hourly check time
  
  // Baseline calibration approach
  float baseline = 0.0;       // Force (grams) when new full bottle is installed
  bool bottlePresent = false; // Track if bottle is currently installed
  
  // Cached sensor values
  int gasLevel = 0;
  float gasDaysRem = 0;
  
  int readADC(); // Helper to read averaged ADC value
  float linearizeADC(int adc); // Linearize FSR using conductance method
   
public:
   //members
  void readGasLevel(); // Reads sensor and updates cached gasLevel
  void updateUsage(); // Updates hourly usage based on current level and time since last update
  void updateDaysRemaining(); // Calculates time remaining based on given usage and updates cached gasDaysRem
  
  // Getters for cached values
  int getGasLevel() const { return gasLevel; }
  float getGasDaysRemaining() const { return gasDaysRem; }
};

#endif
