// SmarterSpa salt-chlorine generator: LED decoder (PLAN.md Phase 5).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Numeric values are published to HA as the "SmarterSpa state" code.
typedef enum {
    SPA_UNKNOWN = 0,         // since boot, no pattern has been steady yet
    SPA_STANDBY = 1,         // white blink every 10 s
    SPA_GEN_OK = 2,          // solid blue
    SPA_GEN_SALT_LOW = 3,    // solid red
    SPA_GEN_SALT_HIGH = 4,   // solid yellow (the manual's "green")
    SPA_SALT_SHUTDOWN = 5,   // solid red + yellow
    SPA_FAULT = 6,           // no lights and no standby blink for > 15 s
} spa_state_t;

esp_err_t spa_leds_init(void);
spa_state_t spa_leds_state(void);
uint32_t spa_leds_generating_hours(void);   // lifetime electrode-hours (NVS)
const char *spa_state_name(spa_state_t s);

#ifdef __cplusplus
}
#endif
