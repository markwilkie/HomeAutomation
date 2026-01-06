#ifndef GASTANK_H
#define GASTANK_H

#include <Arduino.h>
#include "src/logging/logger.h"

extern Logger logger;

//Defines
#define GAS_LEVEL_ANALOG_PIN 11 // GPIO11 for gas level sensor (WiFi must be disabled during read)
#define GAS_NO_BOTTLE_THRESH 1000 // ADC threshold below which bottle is considered removed
#define GAS_NEW_BOTTLE_THRESH 1500 // ADC threshold above which new bottle is detected
#define GAS_ADC_SAMPLES 40 // Number of samples to average for reading
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
