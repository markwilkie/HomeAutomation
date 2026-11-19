// SpaV2 Matter endpoints. Endpoint order is fixed (HA names entities by it);
// see the table in matter_device.cpp and PLAN.md "Matter endpoint map".
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Calibration buttons, in endpoint order.
typedef enum {
    BTN_CAL_PH7 = 0,
    BTN_CAL_PH4,
    BTN_CAL_PH10,
    BTN_CAL_ORP,
    BTN_CAL_CLEAR,
    BTN_COUNT,
} cal_button_t;

// Called from the Matter task when HA turns a calibration button on. Must not block.
typedef void (*cal_button_cb_t)(cal_button_t button);

esp_err_t matter_device_start(cal_button_cb_t cb);

void matter_publish_water_temp(bool valid, float celsius);
void matter_publish_ph(float ph);
void matter_publish_orp(float orp_mv);
void matter_publish_readings_live(bool live);
void matter_publish_pump(bool running, float amps);
void matter_publish_spa_state(int code);
void matter_publish_electrode_hours(uint32_t hours);
void matter_publish_cal_status(int code);
void matter_publish_spa_readout(int value);
void matter_reset_button(cal_button_t button);

#ifdef __cplusplus
}
#endif
