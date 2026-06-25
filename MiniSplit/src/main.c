#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include <time.h>

#include "tuya_client.h"
#include "matter_device.h"
#include "secrets.h"

static const char *TAG = "MAIN";

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t g_app_event_group = NULL;
static tuya_device_status_t g_last_device_status = {0};
static bool g_last_device_status_valid = false;

// Timing configuration
#define STATUS_POLL_INTERVAL_MS 30000   // Poll Tuya every 30 seconds
#define COMMAND_POLL_INTERVAL_MS 5000   // Check Matter commands every 5 seconds
#define RETRY_DELAY_MS 2000             // Wait before retry on error
#define MAX_RETRIES 3                   // Retry up to 3 times before giving up

// State tracking for error recovery
typedef struct {
    uint32_t last_status_update;        // Timestamp of last successful status update
    uint32_t last_command_check;        // Timestamp of last command check
    uint8_t status_poll_failures;       // Consecutive failures
    uint8_t wifi_disconnects;           // Count of disconnections
} sync_state_t;

static sync_state_t g_sync_state = {0};

static int16_t normalize_tuya_setpoint(int16_t temp_c)
{
    if (temp_c < 1600) {
        temp_c = 1600;
    } else if (temp_c > 3000) {
        temp_c = 3000;
    }
    return (int16_t)(((temp_c + 50) / 100) * 100);
}

static uint8_t map_tuya_mode_to_matter(const tuya_device_status_t *device_status)
{
    if (device_status->heat) {
        return 1; // Heat
    }
    if (device_status->mode_dry) {
        return 2; // Cool
    }
    if (device_status->mode_auto || device_status->mode_eco) {
        return 3; // Auto
    }
    return 0; // Off
}

static void apply_status_to_matter(const tuya_device_status_t *device_status)
{
    matter_update_onoff(device_status->switch_state);
    matter_update_local_temperature(device_status->temp_current);
    matter_update_heating_setpoint(device_status->temp_set);
    matter_update_cooling_setpoint(device_status->temp_set);
    matter_update_system_mode(map_tuya_mode_to_matter(device_status));
    matter_update_mode_flags(device_status->mode_auto,
                             device_status->mode_eco,
                             device_status->mode_dry,
                             device_status->heat);
    matter_update_light(device_status->light);
    matter_update_beep(device_status->beep);
}

static void cache_and_apply_status(const tuya_device_status_t *device_status)
{
    g_last_device_status = *device_status;
    g_last_device_status_valid = true;
    apply_status_to_matter(device_status);
}

static void revert_from_last_status_if_available(void)
{
    if (g_last_device_status_valid) {
        apply_status_to_matter(&g_last_device_status);
    }
}

/**
 * @brief Wait until system time is synchronized via SNTP
 */
static esp_err_t wait_for_time_sync(void)
{
    time_t now = 0;
    struct tm timeinfo = {0};
    const int max_retries = 20;

    ESP_LOGI(TAG, "Starting SNTP time sync...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    for (int retry = 0; retry < max_retries; retry++) {
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year >= (2024 - 1900)) {
            ESP_LOGI(TAG, "Time synchronized: %s", asctime(&timeinfo));
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGE(TAG, "SNTP time sync timed out");
    return ESP_ERR_TIMEOUT;
}

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA started, connecting...");
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (g_app_event_group) {
            xEventGroupClearBits(g_app_event_group, WIFI_CONNECTED_BIT);
        }
        g_sync_state.wifi_disconnects++;
        ESP_LOGW(TAG, "WiFi disconnected (count: %u), attempting reconnect...", 
                 g_sync_state.wifi_disconnects);
        esp_wifi_connect();
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        if (g_app_event_group) {
            xEventGroupSetBits(g_app_event_group, WIFI_CONNECTED_BIT);
        }
        g_sync_state.wifi_disconnects = 0;  // Reset disconnect counter
    }
}

/**
 * @brief Initialize WiFi in STA mode
 */
static esp_err_t wifi_init(void)
{
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                                &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                                &wifi_event_handler, NULL));
    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .scan_method = WIFI_FAST_SCAN,
            .bssid_set = false,
            .channel = 0,
            .listen_interval = 0,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi initialization complete");
    return ESP_OK;
}

// ============================================================================
// Phase 3: Bidirectional Integration
// ============================================================================

/**
 * @brief Synchronization task: polls Tuya and updates Matter
 * 
 * Flow:
 * 1. Get current status from Tuya device
 * 2. Update Matter attributes with Tuya data
 * 3. Handle errors and retries
 * 4. Log status for debugging
 */
static void sync_task(void *param)
{
    ESP_LOGI(TAG, "Status synchronization task started (interval: %ums)", 
             STATUS_POLL_INTERVAL_MS);
    
    // Initial delay to let device stabilize
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(STATUS_POLL_INTERVAL_MS));
        
        tuya_device_status_t device_status = {0};
        
        // Attempt to get device status with retries
        esp_err_t result = ESP_FAIL;
        for (uint8_t attempt = 0; attempt < MAX_RETRIES; attempt++) {
            result = tuya_get_device_status(&device_status);
            
            if (result == ESP_OK) {
                g_sync_state.status_poll_failures = 0;  // Reset failure counter
                break;
            }
            
            // Retry with delay
            if (attempt < MAX_RETRIES - 1) {
                ESP_LOGW(TAG, "Status poll attempt %u failed, retrying in %ums...", 
                         attempt + 1, RETRY_DELAY_MS);
                vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
            }
        }
        
        if (result != ESP_OK) {
            g_sync_state.status_poll_failures++;
            ESP_LOGE(TAG, "Failed to get Tuya status (failures: %u)", 
                     g_sync_state.status_poll_failures);
            
            // After multiple failures, consider device offline
            if (g_sync_state.status_poll_failures > 5) {
                ESP_LOGW(TAG, "Multiple status poll failures - device may be offline");
            }
            continue;
        }
        
        // Log current status
        ESP_LOGI(TAG, "Tuya Status Update:");
        ESP_LOGI(TAG, "  Power: %s", device_status.switch_state ? "ON" : "OFF");
        ESP_LOGI(TAG, "  Current Temp: %.1f°C", device_status.temp_current / 100.0f);
        ESP_LOGI(TAG, "  Set Temp: %.1f°C", device_status.temp_set / 100.0f);
        ESP_LOGI(TAG, "  Mode: %s%s%s%s",
                 device_status.mode_auto ? "Auto " : "",
                 device_status.mode_eco ? "Eco " : "",
                 device_status.mode_dry ? "Dry " : "",
                 device_status.heat ? "Heat" : "");
        
        // Update cached status and Matter attributes with source-of-truth Tuya status.
        cache_and_apply_status(&device_status);
        
        g_sync_state.last_status_update = xTaskGetTickCount();
    }
}

/**
 * @brief Command routing task: checks for Matter commands and sends to Tuya
 * 
 * Flow:
 * 1. Check if SmartThings sent any commands
 * 2. Route to appropriate Tuya API call
 * 3. Clear command flag after processing
 * 4. Handle errors gracefully
 */
static void command_task(void *param)
{
    ESP_LOGI(TAG, "Command routing task started (interval: %ums)", 
             COMMAND_POLL_INTERVAL_MS);
    
    // Initial delay
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(COMMAND_POLL_INTERVAL_MS));
        
        g_sync_state.last_command_check = xTaskGetTickCount();
        
        // ===== Check for OnOff command from Matter =====
        bool onoff_pending = matter_get_onoff_command();
        if (onoff_pending) {
            bool desired_onoff = matter_get_onoff_state();
            ESP_LOGI(TAG, "Processing OnOff command from SmartThings: %s",
                     desired_onoff ? "ON" : "OFF");

            esp_err_t result = tuya_set_power(desired_onoff);
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send power command to Tuya");
                revert_from_last_status_if_available();
            } else {
                ESP_LOGI(TAG, "Power command sent successfully");
            }

            matter_clear_onoff_command();
        }
        
        // ===== Check for Light command =====
        if (matter_get_light_command()) {
            bool desired_light = matter_get_light_state();
            ESP_LOGI(TAG, "Processing light command from SmartThings: %s",
                     desired_light ? "ON" : "OFF");

            esp_err_t result = tuya_set_light(desired_light);
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send light command to Tuya");
                revert_from_last_status_if_available();
            } else {
                ESP_LOGI(TAG, "Light command sent successfully");
            }

            matter_clear_light_command();
        }

        // ===== Check for Beep command =====
        if (matter_get_beep_command()) {
            bool desired_beep = matter_get_beep_state();
            ESP_LOGI(TAG, "Processing beep command from SmartThings: %s",
                     desired_beep ? "ON" : "OFF");

            esp_err_t result = tuya_set_beep(desired_beep);
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send beep command to Tuya");
                revert_from_last_status_if_available();
            } else {
                ESP_LOGI(TAG, "Beep command sent successfully");
            }

            matter_clear_beep_command();
        }

        // ===== Check for Heating Setpoint command =====
        int16_t heat_setpoint_cmd = matter_get_heating_setpoint_command();
        if (heat_setpoint_cmd >= 0) {
            ESP_LOGI(TAG, "Processing heating setpoint command: %.1f°C", 
                     heat_setpoint_cmd / 100.0f);
            
            // Send to Tuya
            int16_t expected_setpoint = normalize_tuya_setpoint(heat_setpoint_cmd);
            esp_err_t result = tuya_set_temperature(expected_setpoint);
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send temperature command to Tuya");
                revert_from_last_status_if_available();
            } else {
                tuya_device_status_t verify_status = {0};
                if (tuya_get_device_status(&verify_status) == ESP_OK) {
                    if (verify_status.temp_set != expected_setpoint) {
                        ESP_LOGE(TAG, "Temperature command not applied by Tuya (expected=%d actual=%d)",
                                 expected_setpoint, verify_status.temp_set);
                    } else {
                        ESP_LOGI(TAG, "Temperature command verified (setpoint=%d)", verify_status.temp_set);
                    }
                    cache_and_apply_status(&verify_status);
                } else {
                    ESP_LOGW(TAG, "Temperature command sent but verification poll failed");
                }
            }
            
            matter_clear_heating_setpoint_command();
        }

        // ===== Check for Cooling Setpoint command =====
        int16_t cool_setpoint_cmd = matter_get_cooling_setpoint_command();
        if (cool_setpoint_cmd >= 0) {
            ESP_LOGI(TAG, "Processing cooling setpoint command: %.1f°C", 
                     cool_setpoint_cmd / 100.0f);

            int16_t expected_setpoint = normalize_tuya_setpoint(cool_setpoint_cmd);
            esp_err_t result = tuya_set_temperature(expected_setpoint);
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send cooling temperature command to Tuya");
                revert_from_last_status_if_available();
            } else {
                tuya_device_status_t verify_status = {0};
                if (tuya_get_device_status(&verify_status) == ESP_OK) {
                    if (verify_status.temp_set != expected_setpoint) {
                        ESP_LOGE(TAG, "Cooling command not applied by Tuya (expected=%d actual=%d)",
                                 expected_setpoint, verify_status.temp_set);
                    } else {
                        ESP_LOGI(TAG, "Cooling command verified (setpoint=%d)", verify_status.temp_set);
                    }
                    cache_and_apply_status(&verify_status);
                } else {
                    ESP_LOGW(TAG, "Cooling command sent but verification poll failed");
                }
            }

            matter_clear_cooling_setpoint_command();
        }
        
        // ===== Check for System Mode command =====
        uint8_t mode_cmd = matter_get_system_mode_command();
        if (mode_cmd != 0xFF) {  // 0xFF = no command
            ESP_LOGI(TAG, "Processing mode command from SmartThings: %u", mode_cmd);
            
            // Map Matter mode to Tuya mode
            // 0=off, 1=heat, 2=cool, 3=auto
            esp_err_t result = tuya_set_mode(mode_cmd);
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send mode command to Tuya");
                revert_from_last_status_if_available();
            } else {
                ESP_LOGI(TAG, "Mode command sent successfully");
                tuya_device_status_t verify_status = {0};
                if (tuya_get_device_status(&verify_status) == ESP_OK) {
                    cache_and_apply_status(&verify_status);
                }
            }
            
            matter_clear_mode_command();
        }
    }
}

/**
 * @brief Health monitoring task: logs system status periodically
 */
static void health_task(void *param)
{
    ESP_LOGI(TAG, "Health monitoring task started");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));  // Every 60 seconds
        
        ESP_LOGI(TAG, "=== System Health Check ===");
        ESP_LOGI(TAG, "WiFi Disconnects: %u", g_sync_state.wifi_disconnects);
        ESP_LOGI(TAG, "Status Poll Failures: %u", g_sync_state.status_poll_failures);
        ESP_LOGI(TAG, "Last Status Update: %ums ago",
                 (xTaskGetTickCount() - g_sync_state.last_status_update));
        
        // Get free memory
        ESP_LOGI(TAG, "Free Heap: %u bytes", esp_get_free_heap_size());
        
        // Additional diagnostics can be added here
    }
}

/**
 * @brief Application main entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "\n\n=== MiniSplit Matter Bridge Starting ===\n");
    
    // Initialize NVS flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    g_app_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(g_app_event_group ? ESP_OK : ESP_FAIL);

    // Initialize TCP/IP stack before creating Wi-Fi netifs.
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Initialize event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Initialize WiFi
    ESP_ERROR_CHECK(wifi_init());

    // Wait for WiFi before network-dependent services.
    EventBits_t bits = xEventGroupWaitBits(g_app_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(30000));
    ESP_ERROR_CHECK((bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT);

    // Tuya authentication requires valid system time.
    ESP_ERROR_CHECK(wait_for_time_sync());
    
    // Initialize Tuya client
    ESP_LOGI(TAG, "Initializing Tuya client...");
    ESP_ERROR_CHECK(tuya_client_init(
        TUYA_DEVICE_ID,
        TUYA_CLIENT_ID,
        TUYA_CLIENT_SECRET
    ));
    
    // Initialize Matter device
    ESP_LOGI(TAG, "Initializing Matter device...");
    ESP_ERROR_CHECK(matter_device_init());
    
    // Start Matter commissioning
    ESP_LOGI(TAG, "Starting Matter commissioning...");
    ESP_ERROR_CHECK(matter_start_commissioning());
    
    // ========== Phase 3: Create Integration Tasks ==========
    
    // Status synchronization task (Tuya → Matter)
    xTaskCreate(sync_task, 
                "status_sync",      // Task name
                16384,              // Stack size
                NULL,               // Parameters
                4,                  // Priority
                NULL);              // Task handle
    
    // Command routing task (Matter → Tuya)
    xTaskCreate(command_task,
                "command_route",
                12288,
                NULL,
                3,                  // Lower priority than sync
                NULL);
    
    // Health monitoring task
    xTaskCreate(health_task,
                "health_monitor",
                2048,
                NULL,
                2,
                NULL);
    
    ESP_LOGI(TAG, "\n=== MiniSplit Matter Bridge Ready ===");
    ESP_LOGI(TAG, "Status Sync Interval: %ums", STATUS_POLL_INTERVAL_MS);
    ESP_LOGI(TAG, "Command Poll Interval: %ums", COMMAND_POLL_INTERVAL_MS);
    ESP_LOGI(TAG, "Status: Waiting for Matter commissioning...\n");
}
