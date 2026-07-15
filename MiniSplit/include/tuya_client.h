#ifndef TUYA_CLIENT_H
#define TUYA_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tuya device status structure
 */
typedef struct {
    bool switch_state;          // Power on/off
    int16_t temp_set;           // Target temperature (×100)
    int16_t temp_current;       // Current temperature (×100)
    int16_t temp_set_f;         // Target temperature Fahrenheit
    uint8_t ac_mode;             // Tuya "mode" DP: 0=auto, 1=cool, 2=dry, 3=fan, 4=heat
    bool heat;                   // Auxiliary electric heat enable (not the same as ac_mode==heat)
    bool health;
    bool cleaning;
    bool fresh_air_valve;
    int16_t compressor_frequency; // Compressor running frequency, raw Hz (NOT x10 despite the
                                  // Tuya typeSpec's claim -- see TUYA_DP_REFERENCE.md's scale
                                  // correction note); 0 = idle/off
    int16_t outdoor_temp;         // Outdoor ambient temperature (×100), from "ure" DP
} tuya_device_status_t;

/**
 * @brief Initialize Tuya client with credentials
 * @param device_id Device ID from Tuya platform
 * @param client_id Client ID from Tuya platform
 * @param client_secret Client secret (secure storage recommended)
 * @return ESP_OK on success
 */
esp_err_t tuya_client_init(const char *device_id, const char *client_id, const char *client_secret);

/**
 * @brief Get current device status from Tuya API
 * @param status Pointer to status structure to fill
 * @return ESP_OK on success
 */
esp_err_t tuya_get_device_status(tuya_device_status_t *status);

/**
 * @brief Set device power state
 * @param on True to turn on, false to turn off
 * @return ESP_OK on success
 */
esp_err_t tuya_set_power(bool on);

/**
 * @brief Set target temperature
 * @param temp_c Temperature in Celsius (×100, e.g., 2100 = 21°C)
 * @return ESP_OK on success
 */
esp_err_t tuya_set_temperature(int16_t temp_c);

/**
 * @brief Set operating mode
 * @param mode Tuya "mode" DP: 0=auto, 1=cool, 2=dry, 3=fan, 4=heat
 * @return ESP_OK on success
 */
esp_err_t tuya_set_mode(uint8_t mode);

/**
 * @brief Set the fresh air intake valve on/off. Independent of "mode" --
 *        see TUYA_DP_REFERENCE.md's Fresh Air Module section.
 * @param on True to open the fresh air valve, false to close it
 * @return ESP_OK on success
 */
esp_err_t tuya_set_fresh_air(bool on);

/**
 * @brief Refresh access token (if expired)
 * @return ESP_OK on success
 */
esp_err_t tuya_refresh_token(void);

/**
 * @brief Cleanup/deinit Tuya client
 */
void tuya_client_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // TUYA_CLIENT_H
