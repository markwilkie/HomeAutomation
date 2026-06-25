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
 * @brief Update additional Tuya mode flags for bridge diagnostics/state
 * @param mode_auto True when auto mode is active
 * @param mode_eco True when eco mode is active
 * @param mode_dry True when dry mode is active
 * @param heat True when heat mode is active
 */
void matter_update_mode_flags(bool mode_auto, bool mode_eco, bool mode_dry, bool heat);

/**
 * @brief Update auxiliary Light state from Tuya status
 * @param on_off Light state
 */
void matter_update_light(bool on_off);

/**
 * @brief Update auxiliary Beep state from Tuya status
 * @param on_off Beep state
 */
void matter_update_beep(bool on_off);

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
 * @brief Check if SmartThings sent a Light OnOff command
 * @return true if command is pending, false otherwise
 */
bool matter_get_light_command(void);

/**
 * @brief Get current desired Light state
 * @return true when ON is desired, false when OFF is desired
 */
bool matter_get_light_state(void);

/**
 * @brief Check if SmartThings sent a Beep OnOff command
 * @return true if command is pending, false otherwise
 */
bool matter_get_beep_command(void);

/**
 * @brief Get current desired Beep state
 * @return true when ON is desired, false when OFF is desired
 */
bool matter_get_beep_state(void);

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
 * @brief Clear pending Light command flag
 */
void matter_clear_light_command(void);

/**
 * @brief Clear pending Beep command flag
 */
void matter_clear_beep_command(void);

/**
 * @brief Cleanup/deinit Matter device
 */
void matter_device_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // MATTER_DEVICE_H
