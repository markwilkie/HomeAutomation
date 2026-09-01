#ifndef PAPERTRAIL_LOGGER_H
#define PAPERTRAIL_LOGGER_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Mirror every ESP_LOG line to a Papertrail syslog endpoint over UDP
 *
 * Hooks esp_log_set_vprintf() so all existing ESP_LOGE/W/I/D/V call sites
 * throughout the app get mirrored automatically -- no per-call-site changes
 * needed. Serial/UART output is preserved unchanged (the previous vprintf
 * handler is still called for every line).
 *
 * DNS resolution for host is deferred to the first log line after boot
 * (network isn't up yet at the point this is normally called from
 * app_main()) and re-attempted at most every 30s while unresolved, rather
 * than blocking startup or hammering DNS on every single line if the
 * network is down. Until resolved, log lines are simply not forwarded --
 * UART output is unaffected either way.
 *
 * @param host Papertrail syslog hostname, e.g. "logs4.papertrailapp.com"
 * @param port Papertrail syslog UDP port
 * @param system_name Identifies this device in the Papertrail UI (the
 *                     "system" a search/view filters by)
 * @return ESP_OK on success
 */
esp_err_t papertrail_logger_init(const char *host, uint16_t port, const char *system_name);

#ifdef __cplusplus
}
#endif

#endif // PAPERTRAIL_LOGGER_H
