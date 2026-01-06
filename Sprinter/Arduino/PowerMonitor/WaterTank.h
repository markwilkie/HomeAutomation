#ifndef WATERTANK_H
#define WATERTANK_H

#include <Arduino.h>
#include "src/logging/logger.h"

extern Logger logger;

//Defines
#define WATER_LEVEL_ANALOG_PIN 10 // GPIO10 for water level sensor
#define WATER_PUMP_CURRENT_ANALOG_PIN 11 
#define WATER_PUMP_INDICATOR_LIGHT 12 

#define PUMP_CURRENT_THRESHOLD 2.2 // Remember that we're running the sensor backwards, so less than is more amps
#define WATER_TANK_FULL_LEVEL 1.73 // 33Ω sender (full): 5V * 240/(33+240) = 4.40V, ADC sees 4.40*3.3/5 = 2.90V, but scaled = 1.73V
#define WATER_TANK_EMPTY_LEVEL 2.5 // 240Ω sender (empty): 5V * 240/(240+240) = 2.5V, ADC sees 2.5*3.3/5 = 1.65V, but scaled = 2.5V

class WaterTank {
private:
  //properties
  long waterRemaining;
  int lastLevel = -1;
  unsigned long lastLevelTime = 0;
  float dailyPercentUsed = 0;
  unsigned long lastDayCheck = 0;
  
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
  void updateUsage(); // Updates daily usage based on current level and time since last update
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
