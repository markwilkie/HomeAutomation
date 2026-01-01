#include <Arduino.h>
#include "WaterTank.h"

/*
     3.3V
      |
[ Float Sensor ]  ← 33–240Ω
      |
      |------- A0 (analog input)
      |
[ 33Ω Resistor ]  ← fixed pull-down
      |
     GND


| Sensor Ω | Vout (V) |
| -------- | -------- |
| 33Ω      | 2.5V     |
| 240Ω     |  .6V     |

========================

Current sensor is a 5A ACS712, which outputs 2.5V at 0A, and 0V at 5A when run backwards - which we're doing.  This way we can run 
the output of the sensor directly into the 3.3v pin of the ESP32-S3
*/

int WaterTank::readWaterLevel()
{
    int analogValue = analogRead(WATER_LEVEL_ANALOG_PIN); // 0-4095 for ESP32-S3
    double voltage =(analogValue * 3.3) / 4095;
    double percent = map((voltage*10), WATER_TANK_EMPTY_LEVEL, WATER_TANK_FULL_LEVEL, 0, 100);
    percent = constrain(percent, 0, 100);
    return static_cast<int>(percent);
}

void WaterTank::updateUsage() {
    int currentLevel = readWaterLevel();
    unsigned long now = millis();
    if (lastLevel == -1) {
        lastLevel = currentLevel;
        lastLevelTime = now;
        lastDayCheck = now;
        return;
    }
    // If level drops, accumulate percent used
    if (currentLevel < lastLevel) {
        dailyPercentUsed += (lastLevel - currentLevel);
    }
    lastLevel = currentLevel;
    lastLevelTime = now;
    // Reset daily usage every 24 hours
    if (now - lastDayCheck > 86400000UL) {
        dailyPercentUsed = 0;
        lastDayCheck = now;
    }
}

double WaterTank::getDaysRemaining()  {
    int currentLevel = readWaterLevel();
    if (dailyPercentUsed > 0) {
        return currentLevel / dailyPercentUsed;
    } else {
        return -1; // Not enough data
    }
}

// Pump class implementation
void WaterPump::init() {
    pinMode(WATER_PUMP_INDICATOR_LIGHT, OUTPUT);
    digitalWrite(WATER_PUMP_INDICATOR_LIGHT, LOW);
}

void WaterPump::updateLight() {
  bool running = isRunning();
  unsigned long now = millis();
  if (running) {
    if (!wasRunning) runningStart = now;
    if (now - runningStart > 60000) {
      // Flash the output pin (toggle every 500ms)
      if (((now / 500) % 2) == 0) {
        digitalWrite(WATER_PUMP_INDICATOR_LIGHT, HIGH);
      } else {
        digitalWrite(WATER_PUMP_INDICATOR_LIGHT, LOW);
      }
    } else {
      digitalWrite(WATER_PUMP_INDICATOR_LIGHT, HIGH); // Keep HIGH while running but not yet flashing
    }
  } else {
    runningStart = now;
    digitalWrite(WATER_PUMP_INDICATOR_LIGHT, LOW);
  }
  wasRunning = running;
}

//
// sensor zero pint is 2.5v (half of 5v)
// we're running the sensor backwards so that 0 is 2.5 and 5a is 0 
bool WaterPump::isRunning() {
  int analogValue = analogRead(WATER_PUMP_CURRENT_ANALOG_PIN);
  double voltage = (analogValue / 4095.0) * 3.3;
  return voltage < PUMP_CURRENT_THRESHOLD;
}
