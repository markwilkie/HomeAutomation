#ifndef DISPLAYDATA_H
#define DISPLAYDATA_H


struct DisplayData
{
    int stateOfCharge;
    int stateOfWater;
    int rangeForWater;
    float currentVolts;
    float chargeAmps;
    float drawAmps;
    float batteryHoursRem;
    float waterDaysRem;
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