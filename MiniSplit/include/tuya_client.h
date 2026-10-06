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
    bool light;                  // "light" DP -- read so IR frame construction can preserve the
                                  // unit's real current Light setting instead of a fixed guess
                                  // (see main.c's send_ir_frame(), IR_PROTOCOL_REFERENCE.md state[5]
                                  // bit 0x40)
    uint8_t fan_speed;           // "fan_speed_enum" DP, raw Tuya values 0-7: Stop/Mute/Low/
                                  // Med-Low/Med/Med-High/High/Turbo (TUYA_DP_REFERENCE.md) -- read
                                  // so IR frame construction can preserve the unit's real current
                                  // fan speed instead of a fixed guess (see main.c's
                                  // map_tuya_fan_speed_to_ir(), IR_PROTOCOL_REFERENCE.md state[8]
                                  // bits 0-2)
    int16_t compressor_frequency; // Compressor running frequency, raw Hz (NOT x10 despite the
                                  // Tuya typeSpec's claim -- see TUYA_DP_REFERENCE.md's scale
                                  // correction note); 0 = idle/off
    int16_t outdoor_temp;         // Outdoor ambient temperature (×100), from "ure" DP
} tuya_device_status_t;

/**
 * @brief Clamp a Celsius (×100) setpoint and round it to the nearest whole
 *        Fahrenheit degree -- the single source of truth for
 *        the C->F step, shared by tuya_normalize_setpoint_c(),
 *        tuya_set_temperature() (retired direct-API path), and main.c's
 *        desired-setpoint reconciliation/outage-mismatch checks (what gets
 *        compared against Tuya's polled temp_set_f), so all of them always
 *        agree on the same value.
 *
 * 2026-09-10: was round-to-nearest until a real 73F command was traced
 * end-to-end: HA sent exactly 22.78C (matter-server logs confirmed, no HA-
 * side rounding issue), the IR encoder correctly rounded that to the
 * nearest 0.5C step (23.0C, no half-degree bit -- 22.78 is genuinely closer
 * to 23.0 than 22.5), and the real unit reported back 74F, not the 73F this
 * function predicted for 23.0C under round-to-nearest (23.0C is exactly
 * 73.4F on the linear C->F line, which round-to-nearest correctly rounds
 * down to 73). Cross-checked against the one other value this session had
 * already confirmed live (21.5C + half-degree bit -> confirmed 71F on the
 * real remote, 2026-09-09): 21.5C is 70.7F linearly, which rounds to 71
 * under EITHER round-to-nearest or ceiling (can't distinguish the two rules
 * from that data point alone) -- but 23.0C's 73.4F only becomes 74F under
 * ceiling. Two data points, both consistent with ceiling and only one
 * consistent with round-to-nearest, is why this changed; not exhaustively
 * re-validated against every setpoint in range, so treat as a strong,
 * evidence-based correction rather than a fully proven one if a future
 * capture ever contradicts it.
 *
 * 2026-10-05: contradicted, back to round-to-nearest. That "23.0C" frame
 * really carried the unit's half-degree bit (state[12] 0x04, which the IR
 * encoder left set on every frame -- see build_ir_state_frame()), so the unit
 * held 23.5C = 74.3F -> 74F. Raw Tuya readings from the real remote fit
 * round-to-nearest: 20.5C->69, 21.0C->70, 21.5C->71, 22.0C->72. Ceiling also
 * broke the round trip with tuya_setpoint_f_to_c() (73F -> 2278 -> 74F).
 * @param temp_c_x100 Setpoint in Celsius (×100), any range
 * @return Whole-degree Fahrenheit, clamped to the device's 16-30C range
 */
static inline int16_t tuya_setpoint_c_to_f(int16_t temp_c_x100)
{
    if (temp_c_x100 < 1600) {
        temp_c_x100 = 1600;
    } else if (temp_c_x100 > 3000) {
        temp_c_x100 = 3000;
    }
    return (int16_t)(((int32_t)temp_c_x100 * 9 + 250) / 500 + 32);
}

/**
 * @brief Inverse of tuya_setpoint_c_to_f(): convert a whole Fahrenheit
 *        degree back to Celsius (×100).
 *
 * Shared by tuya_normalize_setpoint_c() (round-trips a Celsius input
 * through this) and main.c's send_stepped_setpoint() (converts an
 * already-stepped whole-F target back to Celsius for the actual DP write),
 * so both directions of this conversion always agree.
 * @param temp_f Whole-degree Fahrenheit
 * @return Celsius (×100)
 */
static inline int16_t tuya_setpoint_f_to_c(int16_t temp_f)
{
    return (int16_t)((((int32_t)temp_f - 32) * 500 + 4) / 9);
}

/**
 * @brief Clamp and round a Matter/Celsius setpoint (×100) to the nearest
 *        whole Fahrenheit degree, then convert back to Celsius ×100.
 *
 * This device's real setpoint granularity is 1°F (the temp_set_f DP, what
 * the physical remote and HA's Fahrenheit-displayed thermostat card step
 * by), not 1°C -- and temp_set itself is further constrained to 0.5°C
 * steps (per TUYA_DP_REFERENCE.md), which this whole-Fahrenheit-degree
 * result is not guaranteed to land on. That mismatch is exactly why
 * main.c's desired-setpoint reconciliation compares against temp_set_f, not
 * temp_set -- see tuya_setpoint_c_to_f(). This Celsius value is what
 * actually gets sent to Tuya and shown on the Matter Thermostat clusters
 * (which are Celsius-native).
 * @param temp_c_x100 Setpoint in Celsius (×100)
 * @return Normalized setpoint in Celsius (×100)
 */
static inline int16_t tuya_normalize_setpoint_c(int16_t temp_c_x100)
{
    int16_t temp_f = tuya_setpoint_c_to_f(temp_c_x100);
    return tuya_setpoint_f_to_c(temp_f);
}

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
