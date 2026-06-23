/**
 * @file tuya_api_test.c
 * @brief Test harness for Tuya API client
 * 
 * This can be compiled standalone to test Tuya API communication
 * without Matter components.
 */

#include "tuya_client.h"
#include "esp_log.h"

static const char *TAG = "TUYA_TEST";

/**
 * @brief Demonstrate Tuya API usage
 */
void tuya_api_demo(void)
{
    ESP_LOGI(TAG, "Starting Tuya API demo...");
    
    // Initialize client with your Tuya credentials
    // These should be loaded from secure storage in production
    const char *device_id = "eb11d9ff75ef37d109pihg";
    const char *client_id = "qt4e7qf9mnagnhptxn37";
    const char *client_secret = "[YOUR_SECRET_HERE]";
    
    if (tuya_client_init(device_id, client_id, client_secret) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Tuya client");
        return;
    }
    
    // ========== Get Device Status ==========
    ESP_LOGI(TAG, "\n=== Getting Device Status ===");
    tuya_device_status_t status = {0};
    if (tuya_get_device_status(&status) == ESP_OK) {
        ESP_LOGI(TAG, "✓ Status retrieved successfully");
        ESP_LOGI(TAG, "  Power: %s", status.switch_state ? "ON" : "OFF");
        ESP_LOGI(TAG, "  Current Temp: %.1f°C", status.temp_current / 100.0f);
        ESP_LOGI(TAG, "  Set Temp: %.1f°C", status.temp_set / 100.0f);
        ESP_LOGI(TAG, "  Mode Auto: %s", status.mode_auto ? "YES" : "NO");
        ESP_LOGI(TAG, "  Mode Eco: %s", status.mode_eco ? "YES" : "NO");
        ESP_LOGI(TAG, "  Mode Dry: %s", status.mode_dry ? "YES" : "NO");
        ESP_LOGI(TAG, "  Heat: %s", status.heat ? "YES" : "NO");
    } else {
        ESP_LOGE(TAG, "✗ Failed to get device status");
        goto cleanup;
    }
    
    // ========== Test Power Control ==========
    ESP_LOGI(TAG, "\n=== Testing Power Control ===");
    if (tuya_set_power(true) == ESP_OK) {
        ESP_LOGI(TAG, "✓ Turned device ON");
    } else {
        ESP_LOGW(TAG, "✗ Failed to send power command");
    }
    
    // ========== Test Temperature Control ==========
    ESP_LOGI(TAG, "\n=== Testing Temperature Control ===");
    // Set to 22°C (2200 in Tuya format: ×100)
    if (tuya_set_temperature(2200) == ESP_OK) {
        ESP_LOGI(TAG, "✓ Set temperature to 22°C");
    } else {
        ESP_LOGW(TAG, "✗ Failed to send temperature command");
    }
    
    // ========== Test Mode Control ==========
    ESP_LOGI(TAG, "\n=== Testing Mode Control ===");
    // 0=auto, 1=eco, 2=dry, 3=heat
    if (tuya_set_mode(0) == ESP_OK) {
        ESP_LOGI(TAG, "✓ Set mode to Auto");
    } else {
        ESP_LOGW(TAG, "✗ Failed to send mode command");
    }
    
    // Wait a bit and get updated status
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "\n=== Getting Updated Device Status ===");
    if (tuya_get_device_status(&status) == ESP_OK) {
        ESP_LOGI(TAG, "✓ Updated status retrieved");
        ESP_LOGI(TAG, "  Power: %s", status.switch_state ? "ON" : "OFF");
        ESP_LOGI(TAG, "  Current Temp: %.1f°C", status.temp_current / 100.0f);
        ESP_LOGI(TAG, "  Set Temp: %.1f°C", status.temp_set / 100.0f);
    } else {
        ESP_LOGE(TAG, "✗ Failed to get updated status");
    }
    
cleanup:
    tuya_client_deinit();
    ESP_LOGI(TAG, "Demo complete");
}
