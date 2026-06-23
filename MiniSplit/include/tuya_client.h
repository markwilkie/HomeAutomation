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
    bool mode_auto;
    bool mode_eco;
    bool mode_dry;
    bool heat;
    bool light;
    bool beep;
    bool health;
    bool cleaning;
    bool fresh_air_valve;
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
 * @param mode Mode: 0=auto, 1=eco, 2=dry, 3=heat
 * @return ESP_OK on success
 */
esp_err_t tuya_set_mode(uint8_t mode);

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
