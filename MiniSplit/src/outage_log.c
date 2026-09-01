/**
 * @file outage_log.c
 * @brief Small NVS-backed ring of outage records: what kind, when it
 *        started, when it ended.
 *
 * Design notes:
 * - Writes to NVS happen only on a start/end/point-event transition, never
 *   on a timer or poll -- a long stretch with no outages means zero NVS
 *   writes, so wear is a non-concern for a structure this size (~200 bytes).
 * - The write side has no network dependency at all (this is purely local
 *   RAM + NVS); only *reading it back* -- via the Matter endpoints in
 *   matter_device.cpp or the /logs HTTP endpoint in log_server.c -- needs
 *   connectivity. See outage_log.h and the plan this was built from for why
 *   that distinction matters for OUTAGE_REASON_THREAD_DISCONNECTED
 *   specifically.
 * - Fixed-size ring (OUTAGE_LOG_MAX_RECORDS), oldest entries silently
 *   dropped once full -- this is a recent-history tool, not an audit log.
 */

#include "outage_log.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "OUTAGE_LOG";

#define OUTAGE_LOG_MAX_RECORDS 20
#define NVS_NAMESPACE "outage_log"
#define NVS_KEY_BLOB "records"

typedef struct {
    uint8_t reason;
    uint8_t detail;
    uint32_t start_epoch;
    uint32_t end_epoch;  // 0 while still open
} outage_record_t;

typedef struct {
    uint8_t count;                                  // number of valid records, up to OUTAGE_LOG_MAX_RECORDS
    uint8_t next_index;                              // ring write position
    outage_record_t records[OUTAGE_LOG_MAX_RECORDS];
} outage_log_blob_t;

static outage_log_blob_t s_log = {0};
static SemaphoreHandle_t s_lock = NULL;

// Caller must hold s_lock.
static void persist_locked(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace for persist");
        return;
    }
    esp_err_t err = nvs_set_blob(handle, NVS_KEY_BLOB, &s_log, sizeof(s_log));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist outage log: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
}

// Caller must hold s_lock. -1 if no open record for this reason.
static int find_open_index_locked(outage_reason_t reason)
{
    uint8_t valid = s_log.count;
    for (uint8_t i = 0; i < valid; i++) {
        if (s_log.records[i].reason == (uint8_t)reason && s_log.records[i].end_epoch == 0) {
            return i;
        }
    }
    return -1;
}

// Caller must hold s_lock.
static void append_locked(outage_reason_t reason, uint8_t detail, uint32_t start_epoch, uint32_t end_epoch)
{
    outage_record_t *slot = &s_log.records[s_log.next_index];
    slot->reason = (uint8_t)reason;
    slot->detail = detail;
    slot->start_epoch = start_epoch;
    slot->end_epoch = end_epoch;

    s_log.next_index = (uint8_t)((s_log.next_index + 1) % OUTAGE_LOG_MAX_RECORDS);
    if (s_log.count < OUTAGE_LOG_MAX_RECORDS) {
        s_log.count++;
    }
}

esp_err_t outage_log_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No prior outage log found -- starting empty");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    size_t len = sizeof(s_log);
    err = nvs_get_blob(handle, NVS_KEY_BLOB, &s_log, &len);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No prior outage log found -- starting empty");
        memset(&s_log, 0, sizeof(s_log));
        return ESP_OK;
    }
    if (err != ESP_OK || len != sizeof(s_log)) {
        ESP_LOGW(TAG, "Stored outage log unreadable/wrong size, starting empty: %s", esp_err_to_name(err));
        memset(&s_log, 0, sizeof(s_log));
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Loaded outage log: %u record(s)", s_log.count);
    return ESP_OK;
}

void outage_log_start(outage_reason_t reason)
{
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }
    if (find_open_index_locked(reason) >= 0) {
        // Already open -- no-op, matching the header's documented behavior.
        xSemaphoreGive(s_lock);
        return;
    }
    time_t now = time(NULL);
    append_locked(reason, 0, (uint32_t)now, 0);
    persist_locked();
    xSemaphoreGive(s_lock);
    ESP_LOGW(TAG, "Outage started: reason=%d", (int)reason);
}

void outage_log_end(outage_reason_t reason)
{
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }
    int idx = find_open_index_locked(reason);
    if (idx < 0) {
        xSemaphoreGive(s_lock);
        return;
    }
    time_t now = time(NULL);
    s_log.records[idx].end_epoch = (uint32_t)now;
    persist_locked();
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "Outage ended: reason=%d", (int)reason);
}

void outage_log_record_point_event(outage_reason_t reason, uint8_t detail)
{
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }
    time_t now = time(NULL);
    append_locked(reason, detail, (uint32_t)now, (uint32_t)now);
    persist_locked();
    xSemaphoreGive(s_lock);
    ESP_LOGW(TAG, "Outage point event: reason=%d detail=%u", (int)reason, (unsigned)detail);
}

bool outage_log_any_active(void)
{
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    bool active = false;
    for (uint8_t i = 0; i < s_log.count; i++) {
        if (s_log.records[i].end_epoch == 0) {
            active = true;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return active;
}

uint8_t outage_log_last_reason(void)
{
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return 0;
    }
    uint8_t reason = 0;
    if (s_log.count > 0) {
        uint8_t last_idx = (uint8_t)((s_log.next_index + OUTAGE_LOG_MAX_RECORDS - 1) % OUTAGE_LOG_MAX_RECORDS);
        reason = s_log.records[last_idx].reason;
    }
    xSemaphoreGive(s_lock);
    return reason;
}

esp_err_t outage_log_write_json(char *buf, size_t buf_len)
{
    if (!buf || buf_len < 3) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    size_t pos = 0;
    buf[pos++] = '[';

    uint8_t valid = s_log.count;
    // Chronological (oldest-first) order: once the ring has wrapped
    // (count == MAX_RECORDS), the oldest entry is at next_index; before
    // that, entries are simply 0..count-1 in insertion order.
    uint8_t start_idx = (valid == OUTAGE_LOG_MAX_RECORDS) ? s_log.next_index : 0;

    for (uint8_t i = 0; i < valid; i++) {
        uint8_t idx = (uint8_t)((start_idx + i) % OUTAGE_LOG_MAX_RECORDS);
        const outage_record_t *r = &s_log.records[idx];
        int n = snprintf(buf + pos, buf_len - pos, "%s{\"reason\":%u,\"detail\":%u,\"start\":%lu,\"end\":%lu}",
                          (i == 0) ? "" : ",", (unsigned)r->reason, (unsigned)r->detail,
                          (unsigned long)r->start_epoch, (unsigned long)r->end_epoch);
        if (n < 0 || (size_t)n >= buf_len - pos) {
            // Out of room -- stop here rather than emit truncated/invalid JSON.
            break;
        }
        pos += (size_t)n;
    }

    xSemaphoreGive(s_lock);

    if (pos + 2 > buf_len) {
        return ESP_ERR_NO_MEM;
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    return ESP_OK;
}
