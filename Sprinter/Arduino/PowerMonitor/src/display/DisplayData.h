#ifndef DISPLAYDATA_H
#define DISPLAYDATA_H


struct DisplayData
{
    int stateOfCharge;
    int stateOfCharge2;
    int stateOfWater;
    int stateOfGas;
    int rangeForWater;
    float currentVolts;
    float batteryVolts;
    float batteryVolts2;
    float chargeAmps;
    float drawAmps;
    float batteryHoursRem;
    float batteryHoursRem2;
    float waterDaysRem;
    float gasDaysRem;
    float batteryTemperature;
    float chargerTemperature;
    float batteryAmpValue;
    float solarAmpValue;
    float alternaterAmpValue;
    bool cmos;
    bool dmos;
    bool heater;
    int currentHertz;
};

#endif