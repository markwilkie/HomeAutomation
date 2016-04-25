#ifndef weather_h
#define weather_h

#include "ByteBuffer.h"

typedef struct reading
{
  float temperature;
  float humidity;
  float dewpoint;
  float inHg;
  float windSpeed;
  float windGust;
  int windDirection;
  float inchHourRain;
} WeatherReading;

//declare global
WeatherReading aReading;  //struct for weather reading
volatile int anemometerCount = 0; //Increments on every interrupt, resets when inner loop is done
unsigned long millisSinceLastSent=0;  //time since last message was sent to web service
ByteBuffer rainBuffer; //circular buffer for rain readings

#endif

