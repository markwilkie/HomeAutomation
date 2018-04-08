#ifndef   _GLOBAL_H_
#define   _GLOBAL_H_

#if ARDUINO >= 100
#include "Arduino.h"       // for delayMicroseconds, digitalPinToBitMask, etc
#else
#include "WProgram.h"      // for delayMicroseconds
#include "pins_arduino.h"  // for digitalPinToBitMask, etc
#endif
#include <inttypes.h>


typedef struct _sensorDev
{
    union
    {
        uint32_t total;
        int32_t temp_humi_total[2];
    };
    union
    {
        uint16_t average_value;
        int8_t temp_humi_average[2];
    };
    int8_t level;
    int8_t last_level;
    int pin;
}sensorDev;

extern sensorDev sensor_dev[4];


#endif
