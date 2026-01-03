#ifndef GASTANK_H
#define GASTANK_H

#include <Arduino.h>

//Defines
#define GAS_LEVEL_ANALOG_PIN 13 // GPIO for gas level sensor (adjust as needed)
#define GAS_TANK_FULL_LEVEL 0.6 // represent full
#define GAS_TANK_EMPTY_LEVEL 2.5 // represent empty

class GasTank {
private:
  //properties
  long gasRemaining;
  int lastLevel = -1;
  unsigned long lastLevelTime = 0;
  float dailyPercentUsed = 0;
  unsigned long lastDayCheck = 0;
   
public:
   //members
  int readGasLevel(); // Returns tank level as 0-100% from analog input
  void updateUsage(); // Updates daily usage based on current level and time since last update
  double getDaysRemaining(); //calculates time remaining based on given usage
};

#endif
