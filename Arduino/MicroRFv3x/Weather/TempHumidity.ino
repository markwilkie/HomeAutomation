/* 
 HTU21D Humidity Sensor Example Code
 By: Nathan Seidle
 SparkFun Electronics
 Date: September 15th, 2013
 License: This code is public domain but you buy me a beer if you use this and we meet someday (Beerware license).
 
 Uses the HTU21D library to display the current humidity and temperature
 
 Open serial monitor at 9600 baud to see readings. Errors 998 if not sensor is detected. Error 999 if CRC is bad.
  
 Hardware Connections (Breakoutboard to Arduino):
 -VCC = 3.3V
 -GND = GND
 -SDA = A4 (use inline 330 ohm resistor if your board is 5V)
 -SCL = A5 (use inline 330 ohm resistor if your board is 5V)

 */
#include <Wire.h>
#include "SparkFunHTU21D.h"

HTU21D myTempHumidity;

void setupTempHumidity()
{
  myTempHumidity.begin();
}

float getTemperature()
{
  float temp = (myTempHumidity.readTemperature() * 9)/5 + 32;
  return temp;
}

float getHumidity()
{
    float humd = myTempHumidity.readHumidity();
    return humd;
}


