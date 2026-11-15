// Atlas Gravity pH / ORP through the analog isolators (PWM), with calibration.
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CAL_STATUS_NONE = 0,         // no calibration action since boot
    CAL_STATUS_OK = 1,
    CAL_STATUS_UNSTABLE = 2,     // reading not steady over the last 30 s
    CAL_STATUS_OUT_OF_RANGE = 3, // implausible for that solution: wrong buffer / bad probe
    CAL_STATUS_CLEARED = 4,
    CAL_STATUS_NO_SIGNAL = 5,    // no PWM from that isolator
    CAL_STATUS_BUSY = 6,         // published at each press so HA always sees a change
} cal_status_t;

typedef enum {
    CAL_CMD_PH7,
    CAL_CMD_PH4,
    CAL_CMD_PH10,
    CAL_CMD_ORP,                 // against ORP_CAL_REF_MV, or the value passed in
    CAL_CMD_CLEAR,               // pH and ORP back to the library defaults
    CAL_CMD_PH_CLEAR,            // pH only (serial PHCAL,CLEAR)
    CAL_CMD_ORP_CLEAR,           // ORP only (serial ORPCAL,CLEAR)
} cal_cmd_t;

typedef struct {
    bool valid;                  // both isolators producing pulses
    float ph;
    float orp_mv;                // calibrated ORP, mV
    float ph_raw_mv;             // library "voltage" before conversion
    float orp_raw_mv;
} probe_reading_t;

esp_err_t probes_init(void);

// Average of the last 10 one-second samples.
probe_reading_t probes_read(void);

// Runs the guarded calibration and stores it in NVS on success. ref_mv is only
// used for CAL_CMD_ORP (<= 0 means ORP_CAL_REF_MV).
cal_status_t probes_calibrate(cal_cmd_t cmd, float ref_mv);

const char *cal_status_name(cal_status_t s);

// Prints the stored calibration points and pH slope.
void probes_log_calibration(void);

#ifdef __cplusplus
}
#endif
