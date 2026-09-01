#ifndef LOG_SERVER_H
#define LOG_SERVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the on-demand log HTTP server
 *
 * Endpoints (reachable at the device's Thread IPv6 address):
 * - GET  /logs         Streams the flash ring log's current contents (plain text)
 * - POST /logs/enable  Erases the ring and starts capturing (see flash_log.h)
 * - POST /logs/disable Stops capturing (data already written is untouched)
 *
 * Runs regardless of whether logging is currently armed, so enable/disable/
 * read are all always reachable.
 *
 * @return ESP_OK on success
 */
esp_err_t log_server_start(void);

#ifdef __cplusplus
}
#endif

#endif // LOG_SERVER_H
