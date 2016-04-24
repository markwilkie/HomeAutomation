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
  aReading.windSpeed=getWindSpeed();
  aReading.windDirection=readADC(ADC_PIN,readVcc())*.35190;  //ratio of 1023 to 360

  //Get rain
  //Each bucket is 0.0204"   http://www.randomuseless.info/weather/calibration/
  long milliElapsed=(millis()-millisSinceLastSent);
  float minutes=((float)milliElapsed)/1000.0/60.0;
  float inchesRain=((float)interruptCount)*.0204;  //counts bucket drops
  aReading.inchHourRain=inchesRain/(minutes/60.0);  //probably going to not be terribly accurate
  aReading.inchMinRain=inchesRain/minutes;

  VERBOSE_PRINT("Millis / Bucket Count: ");
  VERBOSE_PRINT(milliElapsed);
  VERBOSE_PRINT(" ");
  VERBOSE_PRINTLN(interruptCount);
  
  //Detach interrupt again
  detachInterruptPin(); 
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
  // 2:12 bytes: Wind Speed (int)
  // 2:14 bytes: Wind Direction (int)
  // 4:18 bytes: Dew Point (float)
  // 4:22 bytes: Pressure (float)
  // 4:26 bytes: Rain in/hr (float)
  // 4:30 bytes: Rain in/min (float)
  
  //Create weather packet
  payloadData[0]=(uint8_t)UNITNUM; //unit number
  payloadData[1]='W';
  memcpy(&payloadData[2],&(aReading->temperature),4);
  memcpy(&payloadData[6],&(aReading->humidity),4);
  memcpy(&payloadData[10],&(aReading->windSpeed),2);
  memcpy(&payloadData[12],&(aReading->windDirection),2);
  memcpy(&payloadData[14],&(aReading->dewpoint),4);
  memcpy(&payloadData[18],&(aReading->inHg),4);
  memcpy(&payloadData[22],&(aReading->inchHourRain),4);
  memcpy(&payloadData[26],&(aReading->inchMinRain),4);
  int payloadLen=30; 

  VERBOSE_PRINT("Temp / Humidity / Dewpoint: ");
  VERBOSE_PRINT(aReading->temperature);
  VERBOSE_PRINT(" ");
  VERBOSE_PRINT(aReading->humidity);
  VERBOSE_PRINT(" ");
  VERBOSE_PRINTLN(aReading->dewpoint);  

  VERBOSE_PRINT("Pressure: ");
  VERBOSE_PRINTLN(aReading->inHg);

  VERBOSE_PRINT("Wind Speed/direction: ");
  VERBOSE_PRINT(aReading->windSpeed);
  VERBOSE_PRINT(" ");
  VERBOSE_PRINTLN(aReading->windDirection);

  VERBOSE_PRINT("Rain (in/hr in/min): ");
  VERBOSE_PRINT(aReading->inchHourRain);
  VERBOSE_PRINT(" ");
  VERBOSE_PRINTLN(aReading->inchMinRain);

  return payloadLen;
}


