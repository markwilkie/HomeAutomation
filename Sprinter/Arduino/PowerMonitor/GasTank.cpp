#include "GasTank.h"

// Read averaged ADC value
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

// Read gas level with auto-calibration for 1lb disposable propane tanks
int GasTank::readGasLevel()
{
    int adc = readADC();
    float force = linearizeADC(adc); // Get linearized force reading
    
    if (!bottlePresent) {
        // No bottle detected - check for new bottle installation
        // Use force threshold instead of raw ADC
        if (force > 400.0) { // ~400g+ indicates bottle present (tune threshold)
            baseline = force;        // Auto-calibrate to new full bottle force
            bottlePresent = true;
            lastLevel = 100;         // Reset to full
            dailyPercentUsed = 0;    // Reset usage tracking
            
            logger.log(INFO, "Gas Tank: New 1lb propane bottle detected!");
            logger.log(INFO, "  Baseline force: %.1fg (auto-calibrated)", baseline);
        }
        return 0; // No bottle present
    } else {
        // Bottle present - check if removed
        if (force < 50.0) { // <50g indicates bottle removed (tune threshold)
            bottlePresent = false;
            logger.log(INFO, "Gas Tank: Bottle removed (force: %.1fg)", force);
            return 0; // Bottle removed
        }
        
        // Calculate net weight change from baseline (full bottle)
        // As gas is used, weight decreases, so net becomes more negative
        float netForce = force - baseline;
        
        // Convert net force to percentage
        // 1lb propane = ~453 grams
        // netForce starts at 0 (full) and goes negative as fuel is consumed
        float percentage;
        if (netForce >= 0) {
            percentage = 100.0; // Still at or above baseline (full)
        } else {
            // 1lb propane bottle loses ~453g when empty
            float emptyForceOffset = -453.0; // grams
            percentage = 100.0 + (netForce / emptyForceOffset * 100.0);
        }
        
        // Bounds checking
        if (percentage > 100) percentage = 100;
        if (percentage < 0) percentage = 0;
        
        return (int)percentage;
    }
}

void GasTank::updateUsage()
{
    int currentLevel = readGasLevel();
    unsigned long currentTime = millis();

    // Initialize on first run
    if (lastLevel == -1) {
        lastLevel = currentLevel;
        lastLevelTime = currentTime;
        lastDayCheck = currentTime;
        return;
    }

    // Calculate usage every 24 hours
    if (currentTime - lastDayCheck >= 86400000) { // 24 hours in milliseconds
        int levelChange = lastLevel - currentLevel;
        
        if (levelChange > 0) {
            // Update daily usage as rolling average
            if (dailyPercentUsed == 0) {
                dailyPercentUsed = levelChange;
            } else {
                dailyPercentUsed = (dailyPercentUsed * 0.7) + (levelChange * 0.3); // Weighted average
            }
        }
        
        lastLevel = currentLevel;
        lastDayCheck = currentTime;
    }
}

double GasTank::getDaysRemaining()  {
    int currentLevel = readGasLevel();
    if (dailyPercentUsed > 0) {
        return currentLevel / dailyPercentUsed;
    } else {
        return -1; // Not enough data
    }
}
