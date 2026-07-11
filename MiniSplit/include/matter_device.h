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
 * @brief Initialize Matter device endpoint
 *
 * Sets up:
 * - Matter node with Thermostat device type
 * - On/Off cluster for power control
 * - Thermostat cluster for temperature/mode
 * - Attribute callbacks for controller commands
 *
 * @return ESP_OK on success
 */
esp_err_t matter_device_init(void);

/**
 * @brief Register a FreeRTOS event group bit to track network connectivity
 *
 * The network layer (Thread) is now brought up by Matter's own
 * commissioning-driven provisioning rather than app-level pre-connect code,
 * so the app needs a way to learn when it's actually reachable. Call this
 * before matter_start_commissioning(); the given bit is set on
 * kInternetConnectivityChange-established and cleared on lost, mirroring
 * what a WiFi IP_EVENT handler would have done.
 *
 * @param event_group Event group to signal (caller retains ownership)
 * @param connected_bit Bit to set/clear as connectivity changes
 */
void matter_set_network_event_group(EventGroupHandle_t event_group, EventBits_t connected_bit);

/**
 * @brief Start Matter commissioning (BLE advertisement)
 *
 * Enables:
 * - Bluetooth LE advertisement
 * - QR code generation for the controller to scan
 * - Fabric commissioning, including Thread network provisioning (the
 *   commissioner hands over the Thread Operational Dataset as part of this)
 *
 * @return ESP_OK on success
 */
esp_err_t matter_start_commissioning(void);

/**
 * @brief Update Matter OnOff attribute from Tuya status
 * @param on_off Power state (true=ON, false=OFF)
 */
void matter_update_onoff(bool on_off);

/**
 * @brief Update Matter LocalTemperature attribute from Tuya
 * @param temp_c Temperature in Celsius (×100, e.g., 2200 = 22°C)
 */
void matter_update_local_temperature(int16_t temp_c);

/**
 * @brief Update Matter HeatingSetpoint attribute from Tuya
 * @param temp_c Temperature in Celsius (×100)
 */
void matter_update_heating_setpoint(int16_t temp_c);

/**
 * @brief Update Matter CoolingSetpoint attribute from Tuya
 * @param temp_c Temperature in Celsius (×100)
 */
void matter_update_cooling_setpoint(int16_t temp_c);

/**
 * @brief Update Matter SystemMode attribute from Tuya
 * @param mode 0=off, 1=heat, 2=cool, 3=auto
 */
void matter_update_system_mode(uint8_t mode);

/**
 * @brief Update the standalone (BME280) indoor Temperature Sensor endpoint value
 *
 * This is the BME280's own indoor reading -- deliberately separate from the
 * mini-split's indoor reading on the Thermostat's LocalTemperature
 * (matter_update_local_temperature). Only called from env_task, and only when
 * a BME280 is present; there is no Tuya fallback onto this endpoint.
 *
 * @param temp_c Temperature in Celsius (×100, e.g., 2200 = 22°C)
 */
void matter_update_aux_temperature(int16_t temp_c);

/**
 * @brief Update the standalone Humidity Sensor endpoint value
 *
 * Exposed to SmartThings as a Relative Humidity Measurement capability, usable
 * as a routine trigger/condition.
 *
 * @param humidity_centi_pct Relative humidity in percent ×100 (e.g., 5000 = 50%)
 */
void matter_update_aux_humidity(uint16_t humidity_centi_pct);

/**
 * @brief Update compressor load state from the Tuya compressor_frequency DP
 *
 * Writes to two places:
 * - Thermostat PICoolingDemand/PIHeatingDemand sub-attributes (spec-correct,
 *   but SmartThings does not render them in its Thermostat UI).
 * - A dedicated Humidity Sensor endpoint, repurposed for its %RH x100 scale
 *   (an exact, unscaled match for 0-100%) -- see matter_update_compressor_running()
 *   for the actual running state, which lives on its own endpoint.
 *
 * @param percent 0-100
 */
void matter_update_compressor_demand(uint8_t percent);

/**
 * @brief Update whether the compressor is currently running
 *
 * Written to a dedicated Occupancy Sensor endpoint rather than bundled onto
 * the load-percent endpoint's OnOff attribute -- that would render in Home
 * Assistant as an actionable (but non-functional) light toggle. An Occupancy
 * Sensor endpoint instead renders as a read-only binary_sensor (Detected/Clear)
 * with full state history, which is what this actually is.
 *
 * A Contact Sensor (BooleanState cluster) endpoint was tried first and is the
 * more semantically fitting device type, but confirmed on real hardware that
 * this esp-matter/connectedhomeip version has migrated BooleanState's
 * StateValue attribute to a newer C++ cluster class whose live value bypasses
 * esp_matter's generic attribute store -- attribute::update() against it
 * fails with ESP_ERR_NOT_SUPPORTED. OccupancySensing's Occupancy attribute is
 * still on the classic attribute-store path in this SDK version, so it works.
 *
 * @param running true if compressor_frequency > 0
 */
void matter_update_compressor_running(bool running);

/**
 * @brief Update outdoor ambient temperature from the Tuya ure DP
 *
 * Writes to both the Thermostat's OutdoorTemperature sub-attribute (not
 * rendered by SmartThings) and a dedicated standalone temperature_sensor
 * endpoint (which is).
 *
 * @param temp_c Outdoor ambient temperature in Celsius (×100)
 */
void matter_update_outdoor_temperature(int16_t temp_c);

/**
 * @brief Check if the controller sent an OnOff command
 * @return true if command is pending, false otherwise
 */
bool matter_get_onoff_command(void);

/**
 * @brief Get current desired OnOff state from Matter endpoint
 * @return true when ON is desired, false when OFF is desired
 */
bool matter_get_onoff_state(void);

/**
 * @brief Get pending heating setpoint command from the controller
 * @return Setpoint in Celsius (×100), or -1 if no pending command
 */
int16_t matter_get_heating_setpoint_command(void);

/**
 * @brief Get pending cooling setpoint command from the controller
 * @return Setpoint in Celsius (×100), or -1 if no pending command
 */
int16_t matter_get_cooling_setpoint_command(void);

/**
 * @brief Get pending system mode command from the controller
 * @return Mode (0=off, 1=heat, 2=cool, 3=auto), or 0xFF if no pending command
 */
uint8_t matter_get_system_mode_command(void);

/**
 * @brief Clear the pending OnOff command flag
 * Call after processing the command to avoid re-processing
 */
void matter_clear_onoff_command(void);

/**
 * @brief Clear the pending setpoint command flag
 */
void matter_clear_setpoint_command(void);

/**
 * @brief Clear only the pending heating setpoint command flag
 */
void matter_clear_heating_setpoint_command(void);

/**
 * @brief Clear only the pending cooling setpoint command flag
 */
void matter_clear_cooling_setpoint_command(void);

/**
 * @brief Clear the pending mode command flag
 */
void matter_clear_mode_command(void);

/**
 * @brief Cleanup/deinit Matter device
 */
void matter_device_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // MATTER_DEVICE_H
