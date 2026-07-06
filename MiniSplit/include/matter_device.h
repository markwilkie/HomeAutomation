#ifndef MATTER_DEVICE_H
#define MATTER_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

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
 * - Attribute callbacks for SmartThings commands
 * 
 * @return ESP_OK on success
 */
esp_err_t matter_device_init(void);

/**
 * @brief Start Matter commissioning (BLE advertisement)
 * 
 * Enables:
 * - Bluetooth LE advertisement
 * - QR code generation for SmartThings scanning
 * - Fabric commissioning from SmartThings app
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
 * @brief Update the standalone Temperature Sensor endpoint value
 *
 * Exposed to SmartThings as a Temperature Measurement capability, usable as a
 * routine trigger/condition independent of the thermostat's LocalTemperature.
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
 * - A dedicated dimmable_light-style endpoint: OnOff reflects whether the
 *   compressor is actually running (percent > 0), Level reflects load
 *   0-100% scaled to Matter's 0-254 range. This is the one that actually
 *   shows up in the SmartThings app.
 *
 * @param percent 0-100
 */
void matter_update_compressor_demand(uint8_t percent);

/**
 * @brief Update rolling 8h/24h average compressor load
 *
 * Written to two more dedicated LevelControl endpoints (0-254, same scaling
 * as matter_update_compressor_demand), one per window. The custom SmartThings
 * driver maps these onto its "Load Last 8h"/"Load Last 24h" components.
 *
 * @param avg8h_percent 0-100, average load over the last 8 hours
 * @param avg24h_percent 0-100, average load over the last 24 hours
 */
void matter_update_compressor_demand_history(uint8_t avg8h_percent, uint8_t avg24h_percent);

/**
 * @brief Update compressor cycling stats
 *
 * A "cycle" is a complete round trip (off->on->off or on->off->on) -- a
 * single state change alone does not count. Written as raw counts (not
 * percentages) to two more dedicated LevelControl endpoints.
 *
 * @param cycles_moving_window Cycles completed in the trailing 60 minutes (true sliding window)
 * @param cycles_max_24h Highest cycles-in-a-single-hour seen over the last 24 hours
 */
void matter_update_compressor_cycles(uint8_t cycles_moving_window, uint8_t cycles_max_24h);

/**
 * @brief Update how long the compressor has been in its current on/off state
 *
 * Written as a raw (unscaled) integer number of minutes to a dedicated
 * TemperatureMeasurement-repurposed endpoint -- deliberately NOT the usual
 * x100 Celsius encoding, since this isn't a temperature. The custom
 * SmartThings driver reads it as-is.
 *
 * @param minutes Minutes since the compressor's on/off state last changed
 */
void matter_update_compressor_state_duration(uint16_t minutes);

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
 * @brief Check if SmartThings sent an OnOff command
 * @return true if command is pending, false otherwise
 */
bool matter_get_onoff_command(void);

/**
 * @brief Get current desired OnOff state from Matter endpoint
 * @return true when ON is desired, false when OFF is desired
 */
bool matter_get_onoff_state(void);

/**
 * @brief Get pending heating setpoint command from SmartThings
 * @return Setpoint in Celsius (×100), or -1 if no pending command
 */
int16_t matter_get_heating_setpoint_command(void);

/**
 * @brief Get pending cooling setpoint command from SmartThings
 * @return Setpoint in Celsius (×100), or -1 if no pending command
 */
int16_t matter_get_cooling_setpoint_command(void);

/**
 * @brief Get pending system mode command from SmartThings
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
