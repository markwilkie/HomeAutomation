/**
 * @file log_server.c
 * @brief On-demand HTTP access to the flash ring log -- see flash_log.h.
 */

#include "log_server.h"
#include "flash_log.h"

#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "LOG_SERVER";

#define LOG_SERVER_PORT 8080
#define LOG_CHUNK_SIZE 1024

static esp_err_t logs_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");

    char buf[LOG_CHUNK_SIZE];
    size_t offset = 0;
    size_t n;
    while ((n = flash_log_read_chunk(offset, buf, sizeof(buf))) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            return ESP_FAIL;
        }
        offset += n;
    }
    // Terminate the chunked response.
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
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
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = LOG_SERVER_PORT;
    config.uri_match_fn = httpd_uri_match_wildcard;

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

    ESP_LOGI(TAG, "Log server started on port %d (GET /logs, POST /logs/enable, POST /logs/disable)",
             LOG_SERVER_PORT);
    return ESP_OK;
}
