// Tuya Client Implementation - Phase 1: API Communication
// Implements HMAC-SHA256 signed requests to Tuya Cloud API

#include "tuya_client.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "mbedtls/sha256.h"
#include "mbedtls/md.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "TUYA_CLIENT";

#define TUYA_API_HOST "https://openapi.tuyaus.com"
#define TUYA_TOKEN_ENDPOINT "/v1.0/token?grant_type=1"
#define TUYA_STATUS_ENDPOINT "/v1.0/iot-03/devices/status"
#define TUYA_COMMAND_ENDPOINT_FMT "/v1.0/iot-03/devices/%s/commands"

#define HTTP_BUFFER_SIZE 4096

typedef struct {
    char device_id[64];
    char client_id[64];
    char client_secret[128];
    char access_token[256];
    uint64_t token_expiry_ms;
} tuya_client_context_t;

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    bool overflow;
} http_response_accumulator_t;

static tuya_client_context_t g_tuya_ctx = {0};

/**
 * @brief Calculate current time in milliseconds since epoch
 */
static uint64_t get_current_time_ms(void)
{
    return (uint64_t)time(NULL) * 1000;
}

/**
 * @brief Convert hex string to bytes (for signature calculation)
 */
static void bytes_to_hex_string_upper(const uint8_t *bytes, size_t len, char *hex_str)
{
    for (size_t i = 0; i < len; i++) {
        sprintf(hex_str + (i * 2), "%02X", bytes[i]);
    }
    hex_str[len * 2] = '\0';
}

static void bytes_to_hex_string_lower(const uint8_t *bytes, size_t len, char *hex_str)
{
    for (size_t i = 0; i < len; i++) {
        sprintf(hex_str + (i * 2), "%02x", bytes[i]);
    }
    hex_str[len * 2] = '\0';
}

/**
 * @brief Calculate HMAC-SHA256 signature for Tuya API request (Tuya v2)
 */
static esp_err_t calculate_tuya_signature(
    const char *method,
    const char *path,
    const char *body,
    uint64_t time_ms,
    char *signature_hex)
{
    if (!method || !path || !signature_hex) {
        return ESP_ERR_INVALID_ARG;
    }

    // Calculate SHA256 hash of body (or empty string if no body)
    uint8_t body_hash[32];
    const char *body_to_hash = body ? body : "";
    
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);  // 0 = SHA256
    mbedtls_sha256_update(&sha_ctx, (const unsigned char *)body_to_hash, strlen(body_to_hash));
    mbedtls_sha256_finish(&sha_ctx, body_hash);
    mbedtls_sha256_free(&sha_ctx);

    // Convert body hash to hex string
    char body_hash_hex[65];
    bytes_to_hex_string_lower(body_hash, 32, body_hash_hex);

    // Tuya v2 stringToSign format: METHOD + "\n" + CONTENT_HASH + "\n\n" + URL
    size_t string_len = strlen(method) + 1 + strlen(body_hash_hex) + 2 + strlen(path);
    char *string_to_sign = malloc(string_len + 1);
    if (!string_to_sign) {
        ESP_LOGE(TAG, "Out of memory building signature input");
        return ESP_ERR_NO_MEM;
    }

    snprintf(string_to_sign, string_len + 1,
             "%s\n%s\n\n%s",
             method, body_hash_hex, path);

    ESP_LOGD(TAG, "String to sign:\n%s", string_to_sign);

    // Sign payload: client_id + access_token(optional) + t + stringToSign
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%" PRIu64, time_ms);

    const char *access_token_part = "";
    if (strcmp(path, TUYA_TOKEN_ENDPOINT) != 0 && g_tuya_ctx.access_token[0] != '\0') {
        access_token_part = g_tuya_ctx.access_token;
    }

    size_t payload_len = strlen(g_tuya_ctx.client_id) + strlen(access_token_part) +
                         strlen(time_str) + strlen(string_to_sign);
    char *sign_payload = malloc(payload_len + 1);
    if (!sign_payload) {
        free(string_to_sign);
        ESP_LOGE(TAG, "Out of memory building sign payload");
        return ESP_ERR_NO_MEM;
    }

    snprintf(sign_payload, payload_len + 1, "%s%s%s%s",
             g_tuya_ctx.client_id, access_token_part, time_str, string_to_sign);

    // Calculate HMAC-SHA256
    uint8_t hmac_result[32];
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info) {
        free(string_to_sign);
        free(sign_payload);
        ESP_LOGE(TAG, "Failed to get SHA256 md info");
        return ESP_FAIL;
    }
    mbedtls_md_hmac(md_info,
                    (const unsigned char *)g_tuya_ctx.client_secret,
                    strlen(g_tuya_ctx.client_secret),
                    (const unsigned char *)sign_payload,
                    strlen(sign_payload),
                    hmac_result);
    free(string_to_sign);
    free(sign_payload);

    // Convert HMAC to hex string
    bytes_to_hex_string_upper(hmac_result, 32, signature_hex);

    ESP_LOGD(TAG, "Calculated signature: %s", signature_hex);
    return ESP_OK;
}

/**
 * @brief HTTP event handler for esp_http_client
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP Client Error");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (evt->user_data && evt->data && evt->data_len > 0) {
                http_response_accumulator_t *acc = (http_response_accumulator_t *)evt->user_data;
                size_t remaining = (acc->capacity > acc->length) ? (acc->capacity - acc->length - 1) : 0;
                size_t to_copy = (evt->data_len < remaining) ? evt->data_len : remaining;
                if (to_copy > 0) {
                    memcpy(acc->buffer + acc->length, evt->data, to_copy);
                    acc->length += to_copy;
                    acc->buffer[acc->length] = '\0';
                }
                if ((size_t)evt->data_len > to_copy) {
                    acc->overflow = true;
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief Make signed HTTP request to Tuya API
 */
static esp_err_t tuya_api_request(
    const char *method,
    const char *endpoint,
    const char *request_body,
    char *response_buffer,
    size_t response_buffer_len)
{
    if (!method || !endpoint || !response_buffer) {
        return ESP_ERR_INVALID_ARG;
    }

    // Ensure we have a valid token for non-token endpoints.
    if (strcmp(endpoint, TUYA_TOKEN_ENDPOINT) != 0) {
        uint64_t now_ms = get_current_time_ms();
        bool token_missing = (g_tuya_ctx.access_token[0] == '\0');
        bool token_expired = (g_tuya_ctx.token_expiry_ms > 0 && now_ms >= g_tuya_ctx.token_expiry_ms);
        if (token_missing || token_expired) {
            esp_err_t token_err = tuya_refresh_token();
            if (token_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to refresh token before request");
                return token_err;
            }
        }
    }

    // Get current timestamp
    uint64_t time_ms = get_current_time_ms();

    // Calculate signature
    char signature[65] = {0};
    if (calculate_tuya_signature(method, endpoint, request_body, time_ms, signature) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to calculate signature");
        return ESP_FAIL;
    }

    // Build full URL
    char full_url[512];
    snprintf(full_url, sizeof(full_url), "%s%s", TUYA_API_HOST, endpoint);

    response_buffer[0] = '\0';
    http_response_accumulator_t response_acc = {
        .buffer = response_buffer,
        .capacity = response_buffer_len,
        .length = 0,
        .overflow = false,
    };

    // Create HTTP client
    esp_http_client_config_t config = {
        .url = full_url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = &response_acc,
        .buffer_size = HTTP_BUFFER_SIZE,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    // Override method if needed
    if (strcmp(method, "POST") == 0) {
        config.method = HTTP_METHOD_POST;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return ESP_FAIL;
    }

    // Set headers
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "client_id", g_tuya_ctx.client_id);
    esp_http_client_set_header(client, "t", "0");  // Will be set in request
    esp_http_client_set_header(client, "sign_method", "HMAC-SHA256");
    esp_http_client_set_header(client, "sign", signature);

    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%" PRIu64, time_ms);
    esp_http_client_set_header(client, "t", time_str);

    if (g_tuya_ctx.access_token[0] != '\0') {
        esp_http_client_set_header(client, "access_token", g_tuya_ctx.access_token);
    }

    // Set request body if present
    if (request_body) {
        esp_http_client_set_post_field(client, request_body, strlen(request_body));
    }

    // Execute request
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    // Get response status and content
    int status = esp_http_client_get_status_code(client);
    int content_len = esp_http_client_get_content_length(client);

    ESP_LOGI(TAG, "HTTP Status: %d, Content Length: %d", status, content_len);

    if (status != 200) {
        ESP_LOGE(TAG, "Tuya API returned status %d", status);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    if (response_acc.overflow) {
        ESP_LOGE(TAG, "HTTP response truncated (capacity=%u)", (unsigned)response_buffer_len);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Response: %s", response_buffer);

    esp_http_client_cleanup(client);
    return ESP_OK;
}

// ============================================================================
// Public API Implementation
// ============================================================================

/**
 * @brief Validate common Tuya response payload and detect token invalid errors
 */
static esp_err_t tuya_validate_success_response(const char *response_json, bool *token_invalid)
{
    if (!response_json) {
        return ESP_ERR_INVALID_ARG;
    }

    if (token_invalid) {
        *token_invalid = false;
    }

    cJSON *root = cJSON_Parse(response_json);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse Tuya response JSON");
        return ESP_FAIL;
    }

    cJSON *success = cJSON_GetObjectItem(root, "success");
    if (success && cJSON_IsBool(success) && cJSON_IsTrue(success)) {
        cJSON_Delete(root);
        return ESP_OK;
    }

    int code_val = 0;
    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (code) {
        if (cJSON_IsNumber(code)) {
            code_val = code->valueint;
        } else if (cJSON_IsString(code) && code->valuestring) {
            code_val = atoi(code->valuestring);
        }
    }

    cJSON *msg = cJSON_GetObjectItem(root, "msg");
    const char *msg_str = (msg && cJSON_IsString(msg) && msg->valuestring) ? msg->valuestring : "unknown error";

    if (token_invalid && code_val == 1010) {
        *token_invalid = true;
    }

    ESP_LOGE(TAG, "Tuya API command failed: code=%d msg=%s", code_val, msg_str);
    cJSON_Delete(root);
    return ESP_FAIL;
}

/**
 * @brief Send command payload to a specific Tuya device and validate response
 */
static esp_err_t tuya_send_device_command(const char *body_str)
{
    if (!body_str) {
        return ESP_ERR_INVALID_ARG;
    }

    bool token_retry_attempted = false;

    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), TUYA_COMMAND_ENDPOINT_FMT, g_tuya_ctx.device_id);

retry_request:
    char *response_buffer = malloc(HTTP_BUFFER_SIZE);
    if (!response_buffer) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = tuya_api_request("POST", endpoint, body_str, response_buffer, HTTP_BUFFER_SIZE);
    if (result != ESP_OK) {
        free(response_buffer);
        return result;
    }

    bool token_invalid = false;
    result = tuya_validate_success_response(response_buffer, &token_invalid);
    free(response_buffer);

    if (result == ESP_OK) {
        return ESP_OK;
    }

    if (token_invalid && !token_retry_attempted) {
        ESP_LOGW(TAG, "Tuya token invalid during command request, refreshing and retrying");
        if (tuya_refresh_token() == ESP_OK) {
            token_retry_attempted = true;
            goto retry_request;
        }
    }

    return ESP_FAIL;
}

esp_err_t tuya_client_init(const char *device_id, const char *client_id, const char *client_secret)
{
    if (!device_id || !client_id || !client_secret) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(g_tuya_ctx.device_id, device_id, sizeof(g_tuya_ctx.device_id) - 1);
    strncpy(g_tuya_ctx.client_id, client_id, sizeof(g_tuya_ctx.client_id) - 1);
    strncpy(g_tuya_ctx.client_secret, client_secret, sizeof(g_tuya_ctx.client_secret) - 1);
    g_tuya_ctx.token_expiry_ms = 0;

    ESP_LOGI(TAG, "Tuya client initialized for device: %s", device_id);
    
    return ESP_OK;
}

esp_err_t tuya_get_device_status(tuya_device_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }

    bool token_retry_attempted = false;

    // Build endpoint with device_ids parameter
    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint),
             "%s?device_ids=%s", TUYA_STATUS_ENDPOINT, g_tuya_ctx.device_id);

retry_status_request:

    char *response_buffer = malloc(HTTP_BUFFER_SIZE);
    if (!response_buffer) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        return ESP_ERR_NO_MEM;
    }

    // Make API request
    if (tuya_api_request("GET", endpoint, NULL, response_buffer, HTTP_BUFFER_SIZE) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get device status");
        free(response_buffer);
        return ESP_FAIL;
    }

    // Parse JSON response
    cJSON *root = cJSON_Parse(response_buffer);
    free(response_buffer);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse response JSON");
        return ESP_FAIL;
    }

    // Check for success
    cJSON *success = cJSON_GetObjectItem(root, "success");
    if (!success || !success->valueint) {
        cJSON *code = cJSON_GetObjectItem(root, "code");
        if (code && cJSON_IsNumber(code) && code->valueint == 1010) {
            ESP_LOGW(TAG, "Tuya token invalid, refreshing and retrying status request");
            cJSON_Delete(root);
            if (!token_retry_attempted && tuya_refresh_token() == ESP_OK) {
                token_retry_attempted = true;
                goto retry_status_request;
            }
            ESP_LOGE(TAG, "Token refresh failed after token invalid response");
            return ESP_FAIL;
        }
        ESP_LOGE(TAG, "API returned success=false");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Extract result array
    cJSON *result_arr = cJSON_GetObjectItem(root, "result");
    if (!result_arr || !cJSON_IsArray(result_arr) || cJSON_GetArraySize(result_arr) == 0) {
        ESP_LOGE(TAG, "Invalid result in response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Get first device result
    cJSON *device = cJSON_GetArrayItem(result_arr, 0);
    cJSON *status_arr = cJSON_GetObjectItem(device, "status");

    if (!status_arr || !cJSON_IsArray(status_arr)) {
        ESP_LOGE(TAG, "No status array in response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Initialize status structure
    memset(status, 0, sizeof(tuya_device_status_t));

    // Parse status codes
    cJSON *status_item = NULL;
    cJSON_ArrayForEach(status_item, status_arr) {
        const char *code = cJSON_GetStringValue(cJSON_GetObjectItem(status_item, "code"));
        cJSON *value_obj = cJSON_GetObjectItem(status_item, "value");

        if (!code || !value_obj) continue;

        if (strcmp(code, "switch") == 0) {
            status->switch_state = value_obj->valueint;
        } else if (strcmp(code, "temp_set") == 0) {
            status->temp_set = value_obj->valueint;
        } else if (strcmp(code, "temp_current") == 0) {
            status->temp_current = value_obj->valueint;
        } else if (strcmp(code, "temp_set_f") == 0) {
            status->temp_set_f = value_obj->valueint;
        } else if (strcmp(code, "mode_auto") == 0) {
            status->mode_auto = value_obj->valueint;
        } else if (strcmp(code, "mode_eco") == 0) {
            status->mode_eco = value_obj->valueint;
        } else if (strcmp(code, "mode_dry") == 0) {
            status->mode_dry = value_obj->valueint;
        } else if (strcmp(code, "heat") == 0) {
            status->heat = value_obj->valueint;
        } else if (strcmp(code, "light") == 0) {
            status->light = value_obj->valueint;
        } else if (strcmp(code, "beep") == 0) {
            status->beep = value_obj->valueint;
        } else if (strcmp(code, "health") == 0) {
            status->health = value_obj->valueint;
        } else if (strcmp(code, "cleaning") == 0) {
            status->cleaning = value_obj->valueint;
        } else if (strcmp(code, "fresh_air_valve") == 0) {
            status->fresh_air_valve = value_obj->valueint;
        }
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Device status retrieved: switch=%d, temp_current=%d, temp_set=%d",
             status->switch_state, status->temp_current, status->temp_set);

    return ESP_OK;
}

esp_err_t tuya_set_power(bool on)
{
    // Build command JSON
    cJSON *commands = cJSON_CreateArray();
    cJSON *cmd = cJSON_CreateObject();
    cJSON_AddStringToObject(cmd, "code", "switch");
    cJSON_AddBoolToObject(cmd, "value", on);
    cJSON_AddItemToArray(commands, cmd);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "commands", commands);

    char *body_str = cJSON_PrintUnformatted(body);

    ESP_LOGI(TAG, "Setting power to: %s", on ? "ON" : "OFF");
    ESP_LOGD(TAG, "Command body: %s", body_str);

    // Make API request
    esp_err_t result = tuya_send_device_command(body_str);

    free(body_str);
    cJSON_Delete(body);

    return result;
}

esp_err_t tuya_set_temperature(int16_t temp_c)
{
    // Tuya HVAC DPs often reject arbitrary 0.01C values from Matter (e.g. 1822).
    // Normalize to whole-degree C in x100 units and clamp to common mini-split range.
    int16_t requested_temp_c = temp_c;
    if (temp_c < 1600) {
        temp_c = 1600;
    } else if (temp_c > 3000) {
        temp_c = 3000;
    }
    temp_c = (int16_t)(((temp_c + 50) / 100) * 100);

    // Build command JSON
    cJSON *commands = cJSON_CreateArray();

    // Send Celsius setpoint DP (value in x100 units, e.g. 2100 = 21.0C)
    cJSON *cmd_c = cJSON_CreateObject();
    cJSON_AddStringToObject(cmd_c, "code", "temp_set");
    cJSON_AddNumberToObject(cmd_c, "value", temp_c);
    cJSON_AddItemToArray(commands, cmd_c);

    // Some Tuya HVAC profiles only apply setpoint updates when Fahrenheit DP is also provided.
    int16_t temp_f = (int16_t)((temp_c * 9 + 250) / 500 + 32); // Rounded Cx100 -> F integer
    cJSON *cmd_f = cJSON_CreateObject();
    cJSON_AddStringToObject(cmd_f, "code", "temp_set_f");
    cJSON_AddNumberToObject(cmd_f, "value", temp_f);
    cJSON_AddItemToArray(commands, cmd_f);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "commands", commands);

    char *body_str = cJSON_PrintUnformatted(body);

    ESP_LOGI(TAG, "Setting temperature requested=%d normalized=%d (x100 = %.1fC, %dF)",
             requested_temp_c, temp_c, temp_c / 100.0f, temp_f);
    ESP_LOGD(TAG, "Command body: %s", body_str);

    // Make API request
    esp_err_t result = tuya_send_device_command(body_str);

    free(body_str);
    cJSON_Delete(body);

    return result;
}

esp_err_t tuya_set_mode(uint8_t mode)
{
    // Build mode commands based on mode parameter
    // 0=auto, 1=eco, 2=dry, 3=heat
    cJSON *commands = cJSON_CreateArray();
    
    // Set all modes to false first
    const char *all_modes[] = {"mode_auto", "mode_eco", "mode_dry", "heat"};
    for (int i = 0; i < 4; i++) {
        cJSON *cmd = cJSON_CreateObject();
        cJSON_AddStringToObject(cmd, "code", all_modes[i]);
        cJSON_AddBoolToObject(cmd, "value", i == mode);
        cJSON_AddItemToArray(commands, cmd);
    }

    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "commands", commands);

    char *body_str = cJSON_PrintUnformatted(body);

    ESP_LOGI(TAG, "Setting mode to: %u", mode);
    ESP_LOGD(TAG, "Command body: %s", body_str);

    // Make API request
    esp_err_t result = tuya_send_device_command(body_str);

    free(body_str);
    cJSON_Delete(body);

    return result;
}

esp_err_t tuya_refresh_token(void)
{
    char *response_buffer = calloc(1, HTTP_BUFFER_SIZE);
    if (!response_buffer) {
        ESP_LOGE(TAG, "Failed to allocate token response buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = tuya_api_request("GET", TUYA_TOKEN_ENDPOINT, NULL, response_buffer, HTTP_BUFFER_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Token request failed: %s", esp_err_to_name(err));
        free(response_buffer);
        return err;
    }

    cJSON *root = cJSON_Parse(response_buffer);
    free(response_buffer);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse token response JSON");
        return ESP_FAIL;
    }

    cJSON *success = cJSON_GetObjectItem(root, "success");
    if (!success || !cJSON_IsBool(success) || !cJSON_IsTrue(success)) {
        ESP_LOGE(TAG, "Token API returned success=false");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    cJSON *access_token = result ? cJSON_GetObjectItem(result, "access_token") : NULL;
    cJSON *expire_time = result ? cJSON_GetObjectItem(result, "expire_time") : NULL;

    if (!access_token || !cJSON_IsString(access_token) || !access_token->valuestring) {
        ESP_LOGE(TAG, "Token response missing access_token");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    strncpy(g_tuya_ctx.access_token, access_token->valuestring, sizeof(g_tuya_ctx.access_token) - 1);
    g_tuya_ctx.access_token[sizeof(g_tuya_ctx.access_token) - 1] = '\0';

    // Tuya expire_time is typically in seconds.
    uint64_t now_ms = get_current_time_ms();
    if (expire_time && cJSON_IsNumber(expire_time) && expire_time->valuedouble > 0) {
        uint64_t ttl_ms = (uint64_t)expire_time->valuedouble * 1000ULL;
        if (ttl_ms > 60000ULL) {
            ttl_ms -= 60000ULL; // Refresh 1 minute early.
        }
        g_tuya_ctx.token_expiry_ms = now_ms + ttl_ms;
    } else {
        // Fallback: assume 1 hour token validity.
        g_tuya_ctx.token_expiry_ms = now_ms + (60ULL * 60ULL * 1000ULL);
    }

    ESP_LOGI(TAG, "Tuya access token refreshed successfully");
    cJSON_Delete(root);
    return ESP_OK;
}

void tuya_client_deinit(void)
{
    memset(&g_tuya_ctx, 0, sizeof(g_tuya_ctx));
    ESP_LOGI(TAG, "Tuya client deinitialized");
}
