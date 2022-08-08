#ifndef ULP_H
#define ULP_H

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp32/ulp.h"
#include "ulp_main.h"
#include "ulptool.h"

#define WINDFACTOR        3.4         // 2.4 km/h per pulse defined by the Anemometer
#define ULPSLEEP          400 //4000        // amount in microseconds the ULP co-processor sleeps
#define TIMEFACTOR        1000000     // factor between seconds and microseconds
#define TIMESLEEP         10          // amount in seconds the ESP32 sleeps
#define PIN_WIND          GPIO_NUM_14
#define PIN_RAIN          GPIO_NUM_26
#define PIN_AIR           GPIO_NUM_25 // air quality sensor

class ULP {
  
  public:
    ULP();
    void start();
    void sleep();
    uint32_t getRainPulseCount();
    uint32_t getWindPulseCount();
    uint32_t getShortestWindPulse();

  private:
    RTC_DATA_ATTR int bootCount;
    RTC_DATA_ATTR int timeSleep;
    unsigned long startMillis;

    void incrementBootCount();
    void setupULP();
    void setupPins();
    long getElapsed(unsigned long); 
};

#endif