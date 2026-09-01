/**
 * @file log_server.c
 * @brief On-demand HTTP access to the flash ring log -- see flash_log.h.
 */

#include "log_server.h"
#include "flash_log.h"
#include "outage_log.h"
#include "rssi_log.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "LOG_SERVER";

#define LOG_SERVER_PORT 8080
#define LOG_CHUNK_SIZE 1024
#define OUTAGE_JSON_BUF_SIZE 2048
#define RSSI_JSON_BUF_SIZE 2048

// static, not stack-local -- confirmed on real hardware (2026-09-01) that
// stack-local buffers here caused a real overflow once a second one was
// added (see log_server_start()'s stack_size comment). Static avoids
// re-litigating that every time this handler grows another buffer. Guarded
// by s_response_lock since the handler isn't otherwise reentrant-safe with
// shared static buffers -- fine for a diagnostic-only endpoint with one
// expected caller at a time, not worth a dynamic allocation for.
static char s_outage_buf[OUTAGE_JSON_BUF_SIZE];
static char s_rssi_buf[RSSI_JSON_BUF_SIZE];
static char s_log_chunk_buf[LOG_CHUNK_SIZE];
static SemaphoreHandle_t s_response_lock = NULL;

static esp_err_t logs_get_handler(httpd_req_t *req)
{
    if (!s_response_lock || xSemaphoreTake(s_response_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "log server busy, try again\n");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/plain");
    size_t offset = 0;
    size_t n;

    // Outage history first -- always present regardless of whether flash
    // logging itself is currently armed, since it's a separate, always-on
    // NVS-backed record (see outage_log.h), not gated by /logs/enable. One
    // URL for both, per the user's request not to add a separate route.
    if (outage_log_write_json(s_outage_buf, sizeof(s_outage_buf)) == ESP_OK) {
        if (httpd_resp_send_chunk(req, "=== OUTAGES ===\n", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
            goto fail;
        }
        if (httpd_resp_send_chunk(req, s_outage_buf, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
            goto fail;
        }
        if (httpd_resp_send_chunk(req, "\n\n", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
            goto fail;
        }
    }

    // RSSI history -- see rssi_log.h. Same always-on, no-new-route treatment
    // as the outage history above.
    if (rssi_log_write_json(s_rssi_buf, sizeof(s_rssi_buf)) == ESP_OK) {
        if (httpd_resp_send_chunk(req, "=== RSSI ===\n", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
            goto fail;
        }
        if (httpd_resp_send_chunk(req, s_rssi_buf, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
            goto fail;
        }
        if (httpd_resp_send_chunk(req, "\n\n=== LOG ===\n", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
            goto fail;
        }
    }

    while ((n = flash_log_read_chunk(offset, s_log_chunk_buf, sizeof(s_log_chunk_buf))) > 0) {
        if (httpd_resp_send_chunk(req, s_log_chunk_buf, n) != ESP_OK) {
            goto fail;
        }
        offset += n;
    }
    // Terminate the chunked response.
    httpd_resp_send_chunk(req, NULL, 0);
    xSemaphoreGive(s_response_lock);
    return ESP_OK;

fail:
    xSemaphoreGive(s_response_lock);
    return ESP_FAIL;
}

static esp_err_t logs_enable_handler(httpd_req_t *req)
{
    esp_err_t err = flash_log_enable();
    httpd_resp_set_type(req, "text/plain");
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Flash log armed via HTTP request");
        httpd_resp_sendstr(req, "logging enabled\n");
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "failed to enable logging\n");
    }
    return ESP_OK;
}

static esp_err_t logs_disable_handler(httpd_req_t *req)
{
    flash_log_disable();
    ESP_LOGI(TAG, "Flash log disarmed via HTTP request");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "logging disabled\n");
    return ESP_OK;
}

esp_err_t log_server_start(void)
{
    s_response_lock = xSemaphoreCreateMutex();
    if (!s_response_lock) {
        return ESP_ERR_NO_MEM;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = LOG_SERVER_PORT;
    config.uri_match_fn = httpd_uri_match_wildcard;
    // Default is 4096 -- confirmed on real hardware (2026-09-01) that this
    // was not enough once logs_get_handler() gained a second local buffer on
    // top of the existing one (stack overflow on the very first real request
    // after flashing, same failure mode as health_monitor/env_task earlier
    // this session). Those buffers are now static (see s_outage_buf etc.
    // above), not stack-local, which removes the specific cause -- kept
    // doubled anyway as real headroom for whatever this task's own stack
    // needs are, rather than re-tuning to a bare minimum.
    config.stack_size = 8192;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start log server: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t logs_get = {
        .uri = "/logs",
        .method = HTTP_GET,
        .handler = logs_get_handler,
    };
    static const httpd_uri_t logs_enable = {
        .uri = "/logs/enable",
        .method = HTTP_POST,
        .handler = logs_enable_handler,
    };
    static const httpd_uri_t logs_disable = {
        .uri = "/logs/disable",
        .method = HTTP_POST,
        .handler = logs_disable_handler,
    };
    httpd_register_uri_handler(server, &logs_get);
    httpd_register_uri_handler(server, &logs_enable);
    httpd_register_uri_handler(server, &logs_disable);

    ESP_LOGI(TAG, "Log server started on port %d (GET /logs [includes outage + RSSI history], "
                  "POST /logs/enable, POST /logs/disable)",
             LOG_SERVER_PORT);
    return ESP_OK;
}
