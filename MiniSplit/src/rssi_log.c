/**
 * @file rssi_log.c
 * @brief Small NVS-backed ring of Thread parent RSSI samples -- see rssi_log.h.
 *
 * Deliberately a plain time series (fixed-size ring, oldest dropped once
 * full), not tied to outage_log's start/end record shape -- RSSI doesn't
 * have a natural "open/close" the way an outage does, it's just a number
 * over time. 60 samples at the ~5-minute sampling cadence this is fed at
 * (apply_status_to_matter() in main.c) covers roughly 5 hours of history.
 */

#include "rssi_log.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "RSSI_LOG";

#define RSSI_LOG_MAX_SAMPLES 60
#define NVS_NAMESPACE "rssi_log"
#define NVS_KEY_BLOB "samples"

typedef struct {
    uint32_t epoch;
    int8_t rssi;
} rssi_sample_t;

typedef struct {
    uint8_t count;
    uint8_t next_index;
    rssi_sample_t samples[RSSI_LOG_MAX_SAMPLES];
} rssi_log_blob_t;

static rssi_log_blob_t s_log = {0};
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
        ESP_LOGW(TAG, "Failed to persist RSSI log: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
}

esp_err_t rssi_log_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No prior RSSI log found -- starting empty");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    size_t len = sizeof(s_log);
    err = nvs_get_blob(handle, NVS_KEY_BLOB, &s_log, &len);
    nvs_close(handle);
    if (err != ESP_OK || len != sizeof(s_log)) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Stored RSSI log unreadable/wrong size, starting empty: %s", esp_err_to_name(err));
        }
        memset(&s_log, 0, sizeof(s_log));
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Loaded RSSI log: %u sample(s)", s_log.count);
    return ESP_OK;
}

void rssi_log_sample(int8_t rssi_dbm)
{
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }

    rssi_sample_t *slot = &s_log.samples[s_log.next_index];
    slot->epoch = (uint32_t)time(NULL);
    slot->rssi = rssi_dbm;

    s_log.next_index = (uint8_t)((s_log.next_index + 1) % RSSI_LOG_MAX_SAMPLES);
    if (s_log.count < RSSI_LOG_MAX_SAMPLES) {
        s_log.count++;
    }

    persist_locked();
    xSemaphoreGive(s_lock);
}

esp_err_t rssi_log_write_json(char *buf, size_t buf_len)
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
    uint8_t start_idx = (valid == RSSI_LOG_MAX_SAMPLES) ? s_log.next_index : 0;

    for (uint8_t i = 0; i < valid; i++) {
        uint8_t idx = (uint8_t)((start_idx + i) % RSSI_LOG_MAX_SAMPLES);
        const rssi_sample_t *s = &s_log.samples[idx];
        int n = snprintf(buf + pos, buf_len - pos, "%s{\"t\":%lu,\"rssi\":%d}",
                          (i == 0) ? "" : ",", (unsigned long)s->epoch, (int)s->rssi);
        if (n < 0 || (size_t)n >= buf_len - pos) {
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
