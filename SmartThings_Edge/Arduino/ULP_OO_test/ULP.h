#ifndef ULP_H
#define ULP_H

#include <Arduino.h>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp32/ulp.h"

#include "ulp_main.h"

#define ULPSLEEP          400 //4000        // amount in microseconds the ULP co-processor sleeps
#define PIN_WIND          GPIO_NUM_14
#define PIN_RAIN          GPIO_NUM_26
#define PIN_AIR           GPIO_NUM_25 // air quality sensor

class ULP {

  public:
    void setupULP();
    uint32_t getULPRainPulseCount();
    uint32_t getULPWindPulseCount();
    uint32_t getULPShortestWindPulse();
    
  private: 
};

#endif
