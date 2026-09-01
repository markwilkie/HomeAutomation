#ifndef FLASH_LOG_H
#define FLASH_LOG_H

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the flash-backed ring log
 *
 * Hooks esp_log_set_vprintf() so all ESP_LOG output is available to be
 * captured, but writes nothing to flash until flash_log_enable() is called
 * -- logging is off by default, explicitly armed only when actually
 * debugging something, so there is zero flash-write cost during normal
 * operation. Serial/UART output is unaffected either way.
 *
 * Unlike the (now-disabled) Papertrail logger, this never touches the
 * network -- writes go straight to a dedicated flash partition ("log_ring"
 * in partitions.csv), so it can never compete with Matter/Thread for
 * OpenThread message buffers the way remote logging did.
 *
 * @return ESP_OK on success
 */
esp_err_t flash_log_init(void);

/**
 * @brief Arm logging: erase the ring and start capturing from empty
 *
 * Every line is written to flash immediately (no batching), so the most
 * recent line before a crash is already safely on flash by the time the
 * crash happens -- the tradeoff is real per-line flash-write latency while
 * armed, but that cost is local CPU/timing overhead, never network traffic,
 * so it cannot reproduce the Matter buffer-starvation issue Papertrail did
 * regardless of log volume.
 *
 * Armed state is intentionally not persisted across a reboot -- see the
 * design note in flash_log.c for why. Any data already written before a
 * reboot remains on flash and readable via flash_log_read_chunk()
 * regardless of the current armed state.
 *
 * @return ESP_OK on success
 */
esp_err_t flash_log_enable(void);

/**
 * @brief Disarm logging: stop writing further lines (data already on flash is untouched)
 */
void flash_log_disable(void);

/**
 * @brief Whether logging is currently armed
 */
bool flash_log_is_enabled(void);

/**
 * @brief Read back the ring's contents in chronological order (oldest first)
 *
 * Call repeatedly with increasing offset until it returns 0, to stream the
 * whole log out (e.g. from an HTTP handler) without needing a buffer large
 * enough to hold the entire ring at once.
 *
 * @param offset Byte offset into the logical (chronological) log stream
 * @param buf Destination buffer
 * @param buf_len Size of buf
 * @return Number of bytes written to buf (0 once offset reaches the end)
 */
size_t flash_log_read_chunk(size_t offset, char *buf, size_t buf_len);

/**
 * @brief Total number of valid bytes currently in the ring
 */
size_t flash_log_size(void);

#ifdef __cplusplus
}
#endif

#endif // FLASH_LOG_H
