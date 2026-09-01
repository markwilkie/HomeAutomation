/**
 * @file papertrail_logger.c
 * @brief Mirrors ESP_LOG output to Papertrail over UDP syslog (RFC 5424).
 *
 * Message format matches the existing Arduino PapertrailLogger used
 * elsewhere in this repo (Sprinter/PowerMonitor, Sprinter/TripDisplay) for
 * consistency: "<PRI>1 - SYSTEM - - - - MESSAGE" where PRI = facility*8 +
 * severity, facility 16 (local0).
 *
 * This hook runs inside whatever task happens to call ESP_LOG -- which is
 * effectively every task in the app, including ones given deliberately
 * small stacks (health_monitor's 2048 bytes crashed with a stack protection
 * fault the first time this was flashed, from an earlier version of this
 * file that used ~1.2KB of stack-local formatting buffers). All scratch
 * buffers here are therefore static (.bss), not stack-local, so this adds
 * only a few bytes of stack to any caller regardless of its own budget.
 */

#include "papertrail_logger.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define PAPERTRAIL_FACILITY 16
#define PAPERTRAIL_MAX_LINE 400
#define PAPERTRAIL_DNS_RETRY_US (30 * 1000 * 1000LL)
#define PAPERTRAIL_LOCK_TIMEOUT_MS 200

static char s_host[64];
static uint16_t s_port;
static char s_system[32];

static int s_sock = -1;
static struct sockaddr_storage s_addr;
static socklen_t s_addr_len = 0;
static bool s_addr_resolved = false;
static int64_t s_last_dns_attempt_us = 0;

static vprintf_like_t s_prev_vprintf = NULL;
static volatile bool s_in_send = false;
static SemaphoreHandle_t s_lock = NULL;

// Static, not stack-local -- see file header comment. s_line holds the
// vsnprintf'd + ANSI-stripped message; s_packet holds the final RFC5424
// wire packet built from it. Both are only ever touched while s_lock is
// held.
static char s_line[PAPERTRAIL_MAX_LINE];
static char s_packet[PAPERTRAIL_MAX_LINE + 64];

// Resolves s_host/s_port into s_addr, tried lazily (network isn't up yet at
// init time) and rate-limited (a persistently-down network shouldn't retry
// DNS on every single log line -- lwIP's own resolver can itself take up to
// ~7s to give up on a single attempt, see main.c's TUYA_HTTP_TIMEOUT_MS
// comment for the same lesson learned elsewhere in this project).
static bool papertrail_resolve(void)
{
    if (s_addr_resolved) {
        return true;
    }

    int64_t now = esp_timer_get_time();
    if (s_last_dns_attempt_us != 0 && (now - s_last_dns_attempt_us) < PAPERTRAIL_DNS_RETRY_US) {
        return false;
    }
    s_last_dns_attempt_us = now;

    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,   // let DNS64/NAT64 synthesize an AAAA over
                                   // Thread if there's no native IPv4 path,
                                   // same as esp_http_client's own resolution
                                   // for Tuya's API -- see tuya_client.c
        .ai_socktype = SOCK_DGRAM,
    };
    struct addrinfo *res = NULL;
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%u", s_port);

    if (getaddrinfo(s_host, port_str, &hints, &res) != 0 || !res) {
        if (res) {
            freeaddrinfo(res);
        }
        return false;
    }

    memcpy(&s_addr, res->ai_addr, res->ai_addrlen);
    s_addr_len = res->ai_addrlen;
    int family = res->ai_family;
    freeaddrinfo(res);

    if (s_sock >= 0) {
        close(s_sock);
    }
    s_sock = socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        return false;
    }

    s_addr_resolved = true;
    return true;
}

// Caller must hold s_lock.
static void papertrail_send(int severity, const char *message)
{
    if (!papertrail_resolve()) {
        return;
    }

    int pri = PAPERTRAIL_FACILITY * 8 + severity;
    int len = snprintf(s_packet, sizeof(s_packet), "<%d>1 - %s - - - - %s",
                        pri, s_system, message);
    if (len <= 0) {
        return;
    }
    if (len > (int)sizeof(s_packet)) {
        len = sizeof(s_packet);
    }

    // Fire-and-forget: UDP, no retry, no error handling beyond dropping the
    // line. A failed send here must never be allowed to affect UART output
    // (already emitted before this is called) or retry/block the calling
    // task -- see papertrail_vprintf().
    sendto(s_sock, s_packet, len, 0, (struct sockaddr *)&s_addr, s_addr_len);
}

static int papertrail_vprintf(const char *fmt, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);
    int ret = s_prev_vprintf ? s_prev_vprintf(fmt, args_copy) : vprintf(fmt, args_copy);
    va_end(args_copy);

    // Guard against recursion: if anything on the path below (getaddrinfo,
    // socket(), sendto()) ever logs an error via ESP_LOG, that would
    // otherwise re-enter this function.
    if (s_in_send || !s_lock) {
        return ret;
    }

    // Bounded wait, never indefinite -- a task that can't get the lock in
    // time just skips forwarding this one line rather than risk blocking on
    // logging, which every task in the app calls constantly.
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(PAPERTRAIL_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ret;
    }

    va_copy(args_copy, args);
    int n = vsnprintf(s_line, sizeof(s_line), fmt, args_copy);
    va_end(args_copy);
    if (n <= 0) {
        xSemaphoreGive(s_lock);
        return ret;
    }
    if (n >= (int)sizeof(s_line)) {
        n = sizeof(s_line) - 1;
    }

    while (n > 0 && (s_line[n - 1] == '\n' || s_line[n - 1] == '\r')) {
        s_line[--n] = '\0';
    }
    if (n == 0) {
        xSemaphoreGive(s_lock);
        return ret;
    }

    // Strip ESP-IDF's ANSI color escape sequences (\033[...m) in place --
    // Papertrail renders raw text, not terminal colors. Safe since the
    // cleaned output is never longer than the input.
    int ci = 0;
    for (int i = 0; i < n; i++) {
        if (s_line[i] == '\033') {
            while (i < n && s_line[i] != 'm') {
                i++;
            }
            continue;
        }
        s_line[ci++] = s_line[i];
    }
    s_line[ci] = '\0';
    if (ci == 0) {
        xSemaphoreGive(s_lock);
        return ret;
    }

    // ESP-IDF's default format starts with the level letter (E/W/I/D/V),
    // right after the (now-stripped) color code.
    int severity = 6; // Info
    switch (s_line[0]) {
        case 'E': severity = 3; break;
        case 'W': severity = 4; break;
        case 'I': severity = 6; break;
        case 'D': severity = 7; break;
        case 'V': severity = 7; break;
        default: break;
    }

    // Confirmed on real hardware: the OPENTHREAD tag's Info-level output is
    // per-packet trace logging (a line or more for every single mesh frame
    // sent/received), extremely high volume on a busy/marginal link. Every
    // one of those lines was itself triggering a Papertrail UDP send over
    // the very same Thread link it's describing, and that send needs an
    // OpenThread message buffer just like real traffic does -- competing
    // for the same limited pool and measurably worsening the "No available
    // message buffer" exhaustion (and the resulting HA disconnects) rather
    // than just observing it. Genuine OPENTHREAD warnings/errors (like that
    // NoBufs condition itself) are still forwarded; only its routine
    // Info/Debug trace chatter is dropped from Papertrail specifically
    // (UART output above is unaffected either way).
    if (severity >= 6) {
        const char *paren = strchr(s_line, ')');
        const char *tag = paren ? paren + 1 : s_line;
        while (*tag == ' ') {
            tag++;
        }
        if (strncmp(tag, "OPENTHREAD:", 11) == 0) {
            xSemaphoreGive(s_lock);
            return ret;
        }
    }

    s_in_send = true;
    papertrail_send(severity, s_line);
    s_in_send = false;

    xSemaphoreGive(s_lock);
    return ret;
}

esp_err_t papertrail_logger_init(const char *host, uint16_t port, const char *system_name)
{
    if (!host || !system_name) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(s_host, host, sizeof(s_host) - 1);
    s_port = port;
    strncpy(s_system, system_name, sizeof(s_system) - 1);

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    s_prev_vprintf = esp_log_set_vprintf(papertrail_vprintf);
    return ESP_OK;
}
