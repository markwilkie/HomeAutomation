#ifndef weather_h
#define weather_h

typedef struct reading
{
  float temperature;
  float humidity;
  int windSpeed;
  int windDirection;
} WeatherReading;

//declare global
WeatherReading aReading;

#endif

