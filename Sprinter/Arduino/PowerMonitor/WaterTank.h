#ifndef WATERTANK_H
#define WATERTANK_H

#include <Arduino.h>

//Defines
#define WATER_LEVEL_ANALOG_PIN 10 // GPIO10 is ADC1_CH9 on ESP32-S3
#define WATER_PUMP_CURRENT_ANALOG_PIN 11 
#define WATER_PUMP_INDICATOR_LIGHT 12 

#define PUMP_CURRENT_THRESHOLD 2.2 // Remember that we're running the sensor backwards, so less than is more amps
#define WATER_TANK_FULL_LEVEL .6 // represent full
#define WATER_TANK_EMPTY_LEVEL 2.5 // represent empty

class WaterTank {
private:
  //properties
  long waterRemaining;
  int lastLevel = -1;
  unsigned long lastLevelTime = 0;
  float dailyPercentUsed = 0;
  unsigned long lastDayCheck = 0;
   
public:
   //members
  int readWaterLevel(); // Returns tank level as 0-100% from analog input
  void updateUsage(); // Updates daily usage based on current level and time since last update
  double getDaysRemaining(); //calculates time remaining based on given usage
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
