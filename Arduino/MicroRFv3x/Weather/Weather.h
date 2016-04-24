#ifndef weather_h
#define weather_h

typedef struct reading
{
  float temperature;
  float humidity;
  float dewpoint;
  float inHg;
  int windSpeed;
  int windDirection;
  float inchHourRain;
  float inchMinRain;  
} WeatherReading;

//declare global
WeatherReading aReading;
volatile int anemometerCount = 0; //Increments on every interrupt, resets when inner loop is done
unsigned long millisSinceLastSent=0;

#endif

