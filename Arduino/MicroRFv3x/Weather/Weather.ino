/* 
Sets up and does all the weather functions

*/
#include "Weather.h"

void setupWeather()
{
  setupTempHumidity();
}

void pollWeather()
{
  //Read from HTU21D
  aReading.temperature=getTemperature();
  aReading.humidity=getHumidity();

  //zero out others for now
  aReading.windSpeed=0;
  aReading.windDirection=0;
}

void handleWeather(byte* payloadData)
{
    #ifdef LED_PIN
    digitalWrite(LED_PIN, HIGH);
    #endif
  
    VERBOSE_PRINTLN("Sending Weather Payload");   
    int payloadLen=buildWeatherPayload(payloadData,aReading.temperature,aReading.humidity,aReading.windSpeed,aReading.windDirection);
    radioSend(payloadData,payloadLen); //Send weather payload
     
    #ifdef LED_PIN
    digitalWrite(LED_PIN, LOW);
    #endif
}

//Send weather info
int buildWeatherPayload(byte *payloadData,float temp,float humidity,int windSpeed,int windDirection)
{
  //
  // Over-the-air packet definition/spec
  //
  // 1:1 byte:  unit number (uint8)
  // 1:2 byte:  Payload type (S=state, C=context, E=event, W=weather
  // 4:6 bytes: Temperature (float)
  // 4:10 bytes: Humidity (float)
  // 2:12 bytes: Wind Speed (int)
  // 2:14 bytes: Wind Direction (int)
  
  //Create weather packet
  payloadData[0]=(uint8_t)UNITNUM; //unit number
  payloadData[1]='W';
  memcpy(&payloadData[2],&temp,4);
  memcpy(&payloadData[6],&humidity,4);
  memcpy(&payloadData[10],&windSpeed,2);
  memcpy(&payloadData[12],&windDirection,2);
  int payloadLen=14; //So far, the len is 15 w/o any optional data

  VERBOSE_PRINT("Weather: ");
  VERBOSE_PRINT(temp);
  VERBOSE_PRINT(" ");
  VERBOSE_PRINTLN(humidity);

  return payloadLen;
}


