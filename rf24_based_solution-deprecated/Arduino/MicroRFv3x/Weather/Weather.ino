/* 
Sets up and does all the weather functions

*/
#include "Weather.h"


void setupWeather()
{
  //Init time for rain sensor
  millisSinceLastSent=millis();
  
  setupTempHumidity();
  setupPressure();
  setupRain();
}

void pollWeather()
{
  //Because this takes a while, let's turn interrupts back on  (most for rain sensor)
  attachInterruptPin(); 
      
  //Read from HTU21D
  aReading.temperature=getTemperature();
  aReading.humidity=getHumidity();
  aReading.dewpoint=getDewPoint(aReading.humidity,aReading.temperature);

  //Read pressure
  aReading.inHg=getAbsPressure();

  //Get windspeed and direction
  getWindSpeed(&(aReading.windSpeed),&(aReading.windGust)); //takes a while since it's sampling
  aReading.windDirection=readADC(ADC_PIN,readVcc())*.35190;  //ratio of 1023 to 360

  //Detach interrupt again
  detachInterruptPin(); 

  //Get rain
  aReading.inchHourRain=getInchesRain(); 
}

void handleWeather(byte* payloadData)
{
    #ifdef LED_PIN
    digitalWrite(LED_PIN, HIGH);
    #endif
  
    VERBOSE_PRINTLN("Sending Weather Payload");   
    int payloadLen=buildWeatherPayload(payloadData,&aReading);
    radioSend(payloadData,payloadLen); //Send weather payload

    //We need to know time for rain sensor
    millisSinceLastSent=millis();
    interruptCount=0;
     
    #ifdef LED_PIN
    digitalWrite(LED_PIN, LOW);
    #endif
}

//Send weather info
int buildWeatherPayload(byte *payloadData,WeatherReading *aReading)
{
  //
  // Over-the-air packet definition/spec
  //
  // 1:1 byte:  unit number (uint8)
  // 1:2 byte:  Payload type (S=state, C=context, E=event, W=weather
  // 4:6 bytes: Temperature (float)
  // 4:10 bytes: Humidity (float)
  // 2:12 bytes: Wind Direction (int)
  // 4:16 bytes: Wind Speed (float)
  // 4:20 bytes: Wind Gust (float)
  // 4:24 bytes: Dew Point (float)
  // 4:28 bytes: Pressure (float)
  // 4:32 bytes: Rain in/hr (float)
  
  //Create weather packet
  payloadData[0]=(uint8_t)UNITNUM; //unit number
  payloadData[1]='W';
  memcpy(&payloadData[2],&(aReading->temperature),4);
  memcpy(&payloadData[6],&(aReading->humidity),4);
  memcpy(&payloadData[10],&(aReading->windDirection),2);
  memcpy(&payloadData[12],&(aReading->windSpeed),4);
  memcpy(&payloadData[16],&(aReading->windGust),4);
  memcpy(&payloadData[20],&(aReading->dewpoint),4);
  memcpy(&payloadData[24],&(aReading->inHg),4);
  memcpy(&payloadData[28],&(aReading->inchHourRain),4);
  int payloadLen=32; 

  VERBOSE_PRINT("Temp / Humidity / Dewpoint: ");
  VERBOSE_PRINT(aReading->temperature);
  VERBOSE_PRINT(" ");
  VERBOSE_PRINT(aReading->humidity);
  VERBOSE_PRINT(" ");
  VERBOSE_PRINTLN(aReading->dewpoint);  

  VERBOSE_PRINT("Pressure: ");
  VERBOSE_PRINTLN(aReading->inHg);

  VERBOSE_PRINT("Wind Speed/gust/direction: ");
  VERBOSE_PRINT(aReading->windSpeed);
  VERBOSE_PRINT(" ");
  VERBOSE_PRINT(aReading->windGust);
  VERBOSE_PRINT(" ");  
  VERBOSE_PRINTLN(aReading->windDirection);

  VERBOSE_PRINT("Rain (in/hr): ");
  VERBOSE_PRINTLN(aReading->inchHourRain);

  return payloadLen;
}


