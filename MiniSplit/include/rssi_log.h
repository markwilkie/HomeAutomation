#ifndef RSSI_LOG_H
#define RSSI_LOG_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Small NVS-backed ring of Thread parent RSSI samples over time
 *
 * Separate from outage_log.h -- this isn't an outage record, it's a plain
 * time series, sampled on the same 5-minute cadence as everything else in
 * apply_status_to_matter() (main.c). The point is specifically to survive a
 * Thread-disconnected outage: the live Matter sensor (matter_update_thread_rssi())
 * can't report anything while the link is down, by definition, but this
 * local ring already has whatever was sampled right up until it dropped,
 * readable afterward via GET /logs same as the outage history.
 *
 * Unlike outage_log's event-only writes, this persists to NVS on every
 * sample (every ~5 minutes) rather than only on a transition -- a
 * deliberately different tradeoff, still trivial wear for NVS (roughly
 * 288 writes/day of a few hundred bytes).
 */

/**
 * @brief Initialize the RSSI log: load any existing ring from NVS
 * @return ESP_OK on success
 */
esp_err_t rssi_log_init(void);

/**
 * @brief Record one RSSI sample (in dBm) at the current time
 */
void rssi_log_sample(int8_t rssi_dbm);

/**
 * @brief Write the full ring as a JSON array into buf
 *
 * Format: [{"t":1788200000,"rssi":-75}, ...], oldest first. Same
 * truncate-rather-than-corrupt behavior as outage_log_write_json().
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if buf/buf_len are unusable
 */
esp_err_t rssi_log_write_json(char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif // RSSI_LOG_H
