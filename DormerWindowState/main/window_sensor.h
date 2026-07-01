#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configure the window contact GPIO as an input (with pull resistor matching
 * CONFIG_WINDOW_SENSOR_ACTIVE_LOW). Call this before reading the initial
 * state or creating the Matter endpoint.
 */
void app_driver_gpio_init(void);

/**
 * Read the current, debounced window state.
 * Returns true if the window is open, false if closed.
 */
bool app_driver_window_is_open(void);

/**
 * Start the interrupt + debounce task that keeps the Matter Boolean State
 * attribute (cluster 0x0045, attribute StateValue) on `endpoint_id` in sync
 * with the GPIO. Call after the contact_sensor endpoint has been created.
 */
void app_driver_init(uint16_t endpoint_id);

#ifdef __cplusplus
}
#endif
