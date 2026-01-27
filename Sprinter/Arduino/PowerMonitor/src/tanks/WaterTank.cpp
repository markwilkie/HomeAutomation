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
    // Filter out 0 values (analogRead returns 0 on timeout)
    const int NUM_SAMPLES = 10;
    long adcSum = 0;
    int validSamples = 0;
    
    for (int i = 0; i < NUM_SAMPLES; i++) {
        int reading = analogRead(WATER_LEVEL_ANALOG_PIN);
        if (reading > 0) {
            adcSum += reading;
            validSamples++;
        }
        delay(2); // Small delay between reads
    }

    if(validSamples == 0) {
        logger.log(WARNING, "WaterTank::readWaterLevel - All samples invalid (0)");
        waterLevel = 0;
        return;
    }
    
    int analogValue = adcSum / validSamples;
    double voltage = (analogValue * 3.3) / 4095;
    waterVoltage = voltage;  // Cache voltage for logging
    
    // Fixed voltage mapping:
    // Full (33Ω): 2.90V = 100%
    // Empty (240Ω): 1.65V = 0%
    double percent = ((voltage - 1.65) / (2.90 - 1.65)) * 100.0;
    
    percent = constrain(percent, 0, 100);
    int level = static_cast<int>(percent);
    
    Serial.printf("Water Tank: Read level %d (voltage: %fV, ADC: %d averaged)\n", 
               level, voltage, analogValue);
    
    waterLevel = level;
}

void WaterTank::updateUsage() {
    int currentLevel = waterLevel;
    unsigned long now = millis();
    
    // Don't initialize until we have a valid water reading
    if (lastLevel == -1) {
        if (currentLevel > 0) {
            lastLevel = currentLevel;
            lastLevelTime = now;
            lastDayCheck = now;
            Serial.printf("Water updateUsage: Initialized with level %d\n", currentLevel);
        }
        return;
    }
    
    // Calculate usage every hour
    if (now - lastDayCheck >= USAGE_CHECK_INTERVAL) {
        int levelChange = lastLevel - currentLevel;
        
        Serial.printf("Water updateUsage: Hourly check - lastLevel=%d, currentLevel=%d, change=%d\n", 
                   lastLevel, currentLevel, levelChange);
        
        if (levelChange > 0) {
            // Update hourly usage as weighted average (70% old, 30% new)
            if (dailyPercentUsed == 0) {
                dailyPercentUsed = levelChange;
            } else {
                dailyPercentUsed = (dailyPercentUsed * 0.7) + (levelChange * 0.3);
            }
            Serial.printf("Water updateUsage: dailyPercentUsed updated to %f\n", dailyPercentUsed);
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
        waterDaysRem = 0; // Not enough data - show '--'
    }
}

