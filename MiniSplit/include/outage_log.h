#ifndef OUTAGE_LOG_H
#define OUTAGE_LOG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief What kind of outage a record describes
 *
 * 0 is deliberately unused -- outage_log_last_reason() returns 0 to mean
 * "nothing has ever been logged," distinct from any real reason.
 */
typedef enum {
    OUTAGE_REASON_TUYA_UNREACHABLE = 1,     // sync_task's Tuya status poll is failing
    OUTAGE_REASON_SETPOINT_MISMATCH = 2,    // desired setpoint disagrees with Tuya's temp_set_f
    OUTAGE_REASON_DEVICE_RESTART = 3,       // booted from a non-power-on reset; see outage_log_record_point_event()
    OUTAGE_REASON_THREAD_DISCONNECTED = 4,  // OpenThread role dropped to detached/disabled
} outage_reason_t;

/**
 * @brief Initialize the outage log: load any existing ring from NVS
 *
 * Must be called after nvs_flash_init(). If no prior data exists (first
 * boot ever), starts with an empty ring -- not an error.
 *
 * @return ESP_OK on success
 */
esp_err_t outage_log_init(void);

/**
 * @brief Open a new outage record of the given reason, if one isn't already open
 *
 * No-op if a record for this exact reason is already open (start called
 * twice in a row without an intervening end) -- callers are expected to
 * call this only on a genuine bad->worse transition, but this guards
 * against accidental double-starts regardless.
 */
void outage_log_start(outage_reason_t reason);

/**
 * @brief Close the currently-open record of the given reason, if any
 *
 * No-op if no record for this reason is currently open.
 */
void outage_log_end(outage_reason_t reason);

/**
 * @brief Record an instantaneous event with no meaningful duration (start == end == now)
 *
 * Used for OUTAGE_REASON_DEVICE_RESTART: we only learn about an unexpected
 * reset once we're already booting again, so there's no real "start" to
 * track separately from "end."
 *
 * @param reason Outage reason
 * @param detail Reason-specific extra byte (currently only meaningful for
 *               OUTAGE_REASON_DEVICE_RESTART: the raw esp_reset_reason_t value)
 */
void outage_log_record_point_event(outage_reason_t reason, uint8_t detail);

/**
 * @brief Whether any outage record is currently open (end_epoch == 0)
 */
bool outage_log_any_active(void);

/**
 * @brief The most recently recorded reason, or 0 if nothing has ever been logged
 *
 * Persists after the record closes -- this reflects "what was the last
 * outage," not just "what's active right now" (see outage_log_any_active()
 * for that).
 */
uint8_t outage_log_last_reason(void);

/**
 * @brief Write the full ring as a JSON array into buf
 *
 * Format: [{"reason":1,"detail":0,"start":1788200000,"end":1788200300}, ...]
 * (end is 0 for a still-open record). Oldest record first. Truncates
 * silently (still valid JSON) if buf_len is too small for the full ring --
 * callers with a fixed-size buffer should size it generously (20 records
 * is at most a few hundred bytes of JSON).
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if buf/buf_len are unusable
 */
esp_err_t outage_log_write_json(char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif // OUTAGE_LOG_H
