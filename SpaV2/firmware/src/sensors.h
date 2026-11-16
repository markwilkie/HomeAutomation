// Water temperature (DS18B20) and pump current (SCT-013-030 CT).
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t water_temp_init(void);
// false if no probe found / last read failed.
bool water_temp_get(float *celsius);

esp_err_t pump_ct_init(void);
typedef struct {
    bool valid;
    bool running;            // hysteresis CT_ON_A / CT_OFF_A
    float amps;              // RMS x CT_A_PER_V
    float bias_mv;           // should sit near 1650 mV
    unsigned running_s;      // seconds the pump has been continuously on (0 when off)
} pump_state_t;
pump_state_t pump_ct_get(void);

#ifdef __cplusplus
}
#endif
