#include <Arduino.h>
#include "WaterTank.h"

/*
     5V (from external rail)
      |
[ Tank Sender ]  ← 33Ω (full) to 240Ω (empty)
      |
      |------- A0 (analog input via voltage divider)
      |
[ 240Ω Resistor ]  ← fixed pull-down
      |
     GND

Voltage divider: Vout = 5V * R_fixed / (R_sender + R_fixed)

| Sender Ω | Vout (5V rail) | ADC sees (3.3V max) |
| -------- | -------------- | ------------------- |
| 33Ω      | 4.40V          | 2.90V (via divider) |
| 240Ω     | 2.50V          | 1.65V (via divider) |

Note: If 5V connects directly to ESP32 ADC, voltage divider needed to scale 5V→3.3V
Using resistor network: 5V → [R1] → ADC → [R2] → GND where R1/(R1+R2) = 3.3/5 = 0.66

========================

Current sensor is a 5A ACS712, which outputs 2.5V at 0A, and 0V at 5A when run backwards - which we're doing.  This way we can run 
the output of the sensor directly into the 3.3v pin of the ESP32-S3
*/

void WaterTank::readWaterLevel()
{
    // Read multiple samples and average to reduce noise
    const int NUM_SAMPLES = 10;
    long adcSum = 0;
    
    for (int i = 0; i < NUM_SAMPLES; i++) {
        adcSum += analogRead(WATER_LEVEL_ANALOG_PIN);
        delay(2); // Small delay between reads
    }
    
    int analogValue = adcSum / NUM_SAMPLES;
    double voltage = (analogValue * 3.3) / 4095;
    
    // Auto-calibration: Detect fill events (rapid voltage increase)
    unsigned long now = millis();
    if (now - lastCalCheck > 5000) { // Check every 5 seconds
        float voltageChange = voltage - lastVoltage;
        
        // Detect filling: voltage increases >0.3V in 5 seconds AND near expected full range
        if (voltageChange > 0.3 && voltage > 2.5) {
            // Calculate offset from expected full voltage
            float offset = voltage - 2.90; // 2.90V is theoretical full (33Ω)
            
            // Apply same offset to both full and empty calibration points
            fullVoltage = voltage;              // Actual measured full
            emptyVoltage = 1.65 + offset;       // Apply offset to empty (240Ω)
            isCalibrated = true;
            
            logger.log(INFO, "Water Tank: Fill detected! Auto-calibrated.");
            logger.log(INFO, "  Measured full: %.2fV, Offset: %+.2fV", fullVoltage, offset);
            logger.log(INFO, "  Calibrated empty: %.2fV, Range: %.2fV", emptyVoltage, fullVoltage - emptyVoltage);
        }
        
        lastVoltage = voltage;
        lastCalCheck = now;
    }
    
    // Calculate percentage using calibrated or default values
    // Higher voltage = fuller tank
    double percent;
    if (isCalibrated) {
        percent = ((voltage - emptyVoltage) / (fullVoltage - emptyVoltage)) * 100.0;
    } else {
        // Use default theoretical values until calibrated
        percent = ((voltage - 1.65) / (2.90 - 1.65)) * 100.0;
    }
    
    percent = constrain(percent, 0, 100);
    int level = static_cast<int>(percent);
    
    logger.log(INFO, "Water Tank: Read level %d%% (voltage: %.2fV, ADC: %d averaged)", 
               level, voltage, analogValue);
    
    waterLevel = level;
}

void WaterTank::updateUsage() {
    int currentLevel = waterLevel;
    unsigned long now = millis();
    
    // Initialize on first run
    if (lastLevel == -1) {
        lastLevel = currentLevel;
        lastLevelTime = now;
        lastDayCheck = now;
        return;
    }
    
    // Calculate usage every hour
    if (now - lastDayCheck >= USAGE_CHECK_INTERVAL) {
        int levelChange = lastLevel - currentLevel;
        
        if (levelChange > 0) {
            // Update hourly usage as weighted average (70% old, 30% new)
            if (dailyPercentUsed == 0) {
                dailyPercentUsed = levelChange;
            } else {
                dailyPercentUsed = (dailyPercentUsed * 0.7) + (levelChange * 0.3);
            }
        }
        
        lastLevel = currentLevel;
        lastDayCheck = now;
    }
}

void WaterTank::updateDaysRemaining()  {
    int currentLevel = waterLevel;
    if (currentLevel < 5) {
        waterDaysRem = 0; // Tank is essentially empty
    } else if (dailyPercentUsed > 0) {
        waterDaysRem = currentLevel / (dailyPercentUsed * 24);
    } else {
        waterDaysRem = -1; // Not enough data
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
