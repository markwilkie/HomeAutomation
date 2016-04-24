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

float getDewPoint(float humidity,float temperature)
{
  //Calculate dew point
  const float DewConstA = 8.1332; //Constants required to calclulate the partial pressure and dew point. See datasheet page 16
  const float DewConstB = 1762.39;
  const float DewConstC = 235.66;   

  //To calculate the dew point, the partial pressure must be determined first. See datasheet page 16 for details.
  //ParitalPress = 10 ^ (A - (B / (Temp + C)))
  float ParitalPressureFL = (DewConstA - (DewConstB / (temperature + DewConstC)));
  ParitalPressureFL = pow(10, ParitalPressureFL);

  //Dew point is calculated using the partial pressure, humidity and temperature.
  //The datasheet on page 16 doesn't say to use the temperature compensated
  //RH value and is ambiguous. It says "Ambient humidity in %RH, computed from HTU21D(F) sensor".
  //However, as Bjoern Kasper pointed out, when considering a Moliere h-x-diagram, temperature compensated RH% should be used. 
   
  //DewPoint = -(C + B/(log(RH * PartialPress /100) - A ))
  //Arduino doesn't have a LOG base 10 function. But Arduino can use the AVRLibC libraries, so we'll use the "math.h".
  float DewPointFL = (humidity * ParitalPressureFL / 100); //do the innermost brackets
  DewPointFL = log10(DewPointFL) - DewConstA; //we have calculated the denominator of the equation
  DewPointFL = DewConstB / DewPointFL; //the whole fraction of the equation has been calculated
  DewPointFL = -(DewPointFL + DewConstC); //The whole dew point calculation has been performed

  return DewPointFL;
 }

