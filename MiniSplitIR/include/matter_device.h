#ifndef MATTER_DEVICE_H
#define MATTER_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Matter device endpoint
 *
 * Sets up a single Matter Thermostat endpoint (SystemMode, LocalTemperature,
 * OccupiedHeatingSetpoint, OccupiedCoolingSetpoint) with heating+cooling
 * feature flags. No IR send logic and no bound sensor yet -- this is the
 * Milestone 3 skeleton: get the device commissioning and controllable from a
 * Matter controller (Home Assistant, etc.) before wiring in the real AC
 * control path.
 *
 * @return ESP_OK on success
 */
esp_err_t matter_device_init(void);

/**
 * @brief Register a FreeRTOS event group bit to track network connectivity
 *
 * Network bring-up (Thread) is owned by Matter's own commissioning-driven
 * provisioning, not app-level pre-connect code -- see
 * matter_start_commissioning(). This bit is set once Thread/Internet
 * connectivity is established and cleared if it's lost, so app_main() knows
 * when to stop waiting.
 *
 * @param event_group Event group to signal (caller retains ownership)
 * @param connected_bit Bit to set/clear as connectivity changes
 */
void matter_set_network_event_group(EventGroupHandle_t event_group, EventBits_t connected_bit);

/**
 * @brief Start Matter commissioning (BLE advertisement)
 *
 * @return ESP_OK on success
 */
esp_err_t matter_start_commissioning(void);

/**
 * @brief Update the Thermostat's LocalTemperature attribute
 *
 * Milestone 3: no real source yet -- called with a placeholder/test value,
 * or left at its null ("unknown") boot default. Milestone 5 wires this to
 * Device A's bound Temperature Measurement subscription instead.
 *
 * @param temp_c Temperature in Celsius x100 (e.g. 2200 = 22.00C)
 */
void matter_update_local_temperature(int16_t temp_c);

/**
 * @brief Check whether the controller sent a SystemMode command since the
 *        last matter_clear_system_mode_command() call
 * @return true if a command is pending
 */
bool matter_get_system_mode_command_pending(void);

/**
 * @brief Get the last SystemMode value written by a controller
 * @return Matter SystemMode enum value (0=Off, 3=Cool, 4=Heat, 1=Auto, etc.)
 */
uint8_t matter_get_system_mode(void);

/**
 * @brief Clear the pending SystemMode command flag
 */
void matter_clear_system_mode_command(void);

/**
 * @brief Check whether the controller wrote a new cooling setpoint since the
 *        last matter_clear_cooling_setpoint_command() call
 */
bool matter_get_cooling_setpoint_command_pending(void);

/**
 * @brief Get the current OccupiedCoolingSetpoint value
 * @return Setpoint in Celsius x100
 */
int16_t matter_get_cooling_setpoint(void);

/**
 * @brief Clear the pending cooling setpoint command flag
 */
void matter_clear_cooling_setpoint_command(void);

/**
 * @brief Check whether the controller wrote a new heating setpoint since the
 *        last matter_clear_heating_setpoint_command() call
 */
bool matter_get_heating_setpoint_command_pending(void);

/**
 * @brief Get the current OccupiedHeatingSetpoint value
 * @return Setpoint in Celsius x100
 */
int16_t matter_get_heating_setpoint(void);

/**
 * @brief Clear the pending heating setpoint command flag
 */
void matter_clear_heating_setpoint_command(void);

#ifdef __cplusplus
}
#endif

#endif // MATTER_DEVICE_H
