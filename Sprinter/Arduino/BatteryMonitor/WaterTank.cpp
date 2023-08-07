#include <Arduino.h>
#include "WaterTank.h"


void WaterTank::init()
{
    pinMode(WATER_SENSOR0_PIN, INPUT); 
    pinMode(WATER_SENSOR1_PIN, INPUT); 
    pinMode(WATER_SENSOR2_PIN, INPUT); 
}

int WaterTank::readLevel()
{
    stateOfWater=0;

    //Sensor zero starts at the top of the tank  
    if(digitalRead(WATER_SENSOR2_PIN))
        stateOfWater=WATER_SENSOR2_LEVEL;
    if(digitalRead(WATER_SENSOR1_PIN))
        stateOfWater=WATER_SENSOR1_LEVEL;
    if(digitalRead(WATER_SENSOR0_PIN))
        stateOfWater=WATER_SENSOR0_LEVEL;

    return stateOfWater;
}