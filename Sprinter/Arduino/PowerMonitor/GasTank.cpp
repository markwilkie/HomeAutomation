#include "GasTank.h"

//Read analog level from sensor
int GasTank::readGasLevel()
{
    //Get current voltage based on analog reading
    int analogValue = analogRead(GAS_LEVEL_ANALOG_PIN);
    float voltage = (analogValue / 4095.0) * 3.3; 

    //calc percentage based on voltage (ADJUST THESE VALUES based on your sensor)
    float percentage = 100.0 - ((voltage - GAS_TANK_FULL_LEVEL) / (GAS_TANK_EMPTY_LEVEL - GAS_TANK_FULL_LEVEL) * 100.0);
    
    //bounds checking
    if (percentage > 100) percentage = 100;
    if (percentage < 0) percentage = 0;
    
    return (int)percentage;
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
