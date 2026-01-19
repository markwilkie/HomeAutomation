#include "GasTank.h"
#include "src/wifi/wifi.h"

extern VanWifi wifi;

// Read averaged ADC value
// Note: WiFi must be disabled before reading GPIO11 due to hardware conflict
int GasTank::readADC() {
    long sum = 0;
    for (int i = 0; i < GAS_ADC_SAMPLES; i++) {
        sum += analogRead(GAS_LEVEL_ANALOG_PIN);
        delay(2);
    }
    return sum / GAS_ADC_SAMPLES;
}

// Linearize FSR reading using conductance (1/R) method
// FSR C10 has logarithmic R vs force, but linear 1/R vs force
// y = 0.0003x + 0.1329 where y=1/R (mS), x=force (grams)
float GasTank::linearizeADC(int adc) {
    // Convert ADC to voltage (ESP32: 12-bit ADC, 3.3V reference)
    float voltage = (adc / 4095.0) * 3.3;
    
    // For voltage divider: Vout = Vin * R_fixed / (R_fsr + R_fixed)
    // Solving for R_fsr: R_fsr = R_fixed * (Vin - Vout) / Vout
    // Assume 10kΩ fixed resistor (adjust if different)
    float R_fixed = 10000.0; // ohms
    float Vin = 3.3;
    
    if (voltage < 0.01) return 0; // Prevent division by zero
    
    float R_fsr = R_fixed * (Vin - voltage) / voltage;
    
    // Calculate conductance (1/R) in milli-siemens
    float conductance = 1000.0 / R_fsr; // mS
    
    // Using the linear equation from datasheet: conductance = 0.0003 * force + 0.1329
    // Solve for force: force = (conductance - 0.1329) / 0.0003
    float force = (conductance - 0.1329) / 0.0003; // grams
    
    if (force < 0) force = 0;
    
    return force; // Returns linearized force in grams
}

// Read gas level using calibrated ADC values
// ADC 1400 = empty (0%), ADC 1970 = full (100%)
#define GAS_ADC_EMPTY 1400
#define GAS_ADC_FULL  1970

void GasTank::readGasLevel()
{
    int adc = readADC();

    // Simple linear mapping from ADC to percentage
    // ADC range: 1400 (empty) to 1970 (full) = 570 units
    float percentage = ((float)(adc - GAS_ADC_EMPTY) / (float)(GAS_ADC_FULL - GAS_ADC_EMPTY)) * 100.0;
    
    // Bounds checking
    if (percentage > 100) percentage = 100;
    if (percentage < 0) percentage = 0;
    
    gasLevel = static_cast<int>(percentage);
    
    logger.log(INFO, "Gas Tank: Level %d (ADC: %d)", gasLevel, adc);
}

void GasTank::updateUsage()
{
    int currentLevel = gasLevel; // Use cached value
    unsigned long currentTime = millis();

    // Don't initialize until we have a valid gas reading
    // (gasLevel is 0 until readGasLevel() is called and bottle is present)
    if (lastLevel == -1) {
        if (currentLevel > 0) {
            lastLevel = currentLevel;
            lastLevelTime = currentTime;
            lastDayCheck = currentTime;
            logger.log(INFO, "Gas updateUsage: Initialized with level %d", currentLevel);
        }
        return;
    }

    // Calculate usage every hour
    if (currentTime - lastDayCheck >= USAGE_CHECK_INTERVAL) {
        int levelChange = lastLevel - currentLevel;
        
        logger.log(INFO, "Gas updateUsage: Hourly check - lastLevel=%d, currentLevel=%d, change=%d", 
                   lastLevel, currentLevel, levelChange);
        
        if (levelChange > 0) {
            // Update hourly usage as weighted average (70% old, 30% new)
            if (dailyPercentUsed == 0) {
                dailyPercentUsed = levelChange;
            } else {
                dailyPercentUsed = (dailyPercentUsed * 0.7) + (levelChange * 0.3);
            }
            logger.log(INFO, "Gas updateUsage: dailyPercentUsed updated to %f", dailyPercentUsed);
        }
        
        lastLevel = currentLevel;
        lastDayCheck = currentTime;
    }
}

void GasTank::updateDaysRemaining() {
    int currentLevel = gasLevel; // Use cached value
    if (currentLevel < 5) {
        gasDaysRem = 0; // Tank is essentially empty
    } else if (dailyPercentUsed > 0) {
        gasDaysRem = currentLevel / (dailyPercentUsed * 24);
    } else {
        gasDaysRem = -1; // Not enough data
    }
}
