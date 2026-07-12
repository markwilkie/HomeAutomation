#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include <time.h>
#include <math.h>

#include "tuya_client.h"
#include "matter_device.h"
#include "bme280.h"
#include "secrets.h"

static const char *TAG = "MAIN";

// Set once Matter's network layer (Thread) reports connectivity -- see
// matter_set_network_event_group() / app_chip_event_handler() in
// matter_device.cpp. Network bring-up is owned by Matter's commissioning
// flow now, not app-level pre-connect code, so this can take anywhere from
// a few seconds (already-commissioned reboot) to indefinitely (first-time
// commissioning, waiting on the user).
#define NETWORK_CONNECTED_BIT BIT0

static EventGroupHandle_t g_app_event_group = NULL;
static tuya_device_status_t g_last_device_status = {0};
static bool g_last_device_status_valid = false;

// Timing configuration
#define STATUS_POLL_INTERVAL_MS 30000   // Poll Tuya every 30 seconds
#define COMMAND_POLL_INTERVAL_MS 5000   // Check Matter commands every 5 seconds
#define ENV_POLL_INTERVAL_MS 30000      // Read BME280 every 30 seconds
#define RETRY_DELAY_MS 2000             // Wait before retry on error
#define MAX_RETRIES 3                   // Retry up to 3 times before giving up

// State tracking for error recovery
typedef struct {
    uint32_t last_status_update;        // Timestamp of last successful status update
    uint32_t last_command_check;        // Timestamp of last command check
    uint8_t status_poll_failures;       // Consecutive failures
    uint8_t network_disconnects;        // Count of connectivity drops
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

// The product spec claims compressor_frequency is x10-scaled (max 1500 = 150.0Hz),
// but live readings show it reporting the same raw, unscaled Hz value as
// outdoor_comptar_freqrun (max 150) -- e.g. both read 14 simultaneously. Treat it
// as raw Hz to match observed behavior rather than the spec's stated scale.
//
// 150Hz is the shared product-family typeSpec ceiling, not this unit's real
// operating range. Calibrated instead against this unit's observed bands
// (idle/low ~15-25Hz, steady-state rated cooling ~50-70Hz, boost/turbo
// ~90-130Hz, occasionally spiking toward 150Hz briefly) so the resulting
// percentage reads intuitively: idle/low ~12-19%, steady-state ~38-54%,
// boost/turbo ~69-100%. Rare spikes above 130Hz simply clip at 100%.
#define COMPRESSOR_FREQUENCY_MAX 130

static uint8_t compressor_demand_percent(const tuya_device_status_t *device_status)
{
    int32_t freq = device_status->compressor_frequency;
    if (freq <= 0) {
        return 0;
    }
    if (freq > COMPRESSOR_FREQUENCY_MAX) {
        freq = COMPRESSOR_FREQUENCY_MAX;
    }
    return (uint8_t)((freq * 100) / COMPRESSOR_FREQUENCY_MAX);
}

static const char *ac_mode_name(uint8_t ac_mode)
{
    static const char *const names[] = {"Auto", "Cool", "Dry", "Fan", "Heat"};
    return (ac_mode < (sizeof(names) / sizeof(names[0]))) ? names[ac_mode] : "Unknown";
}

// Tuya "mode" DP: 0=auto, 1=cool, 2=dry, 3=fan, 4=heat.
// Matter Thermostat SystemModeEnum: kOff=0, kAuto=1, kCool=3, kFanOnly=7, kDry=8, kHeat=4.
static uint8_t map_tuya_mode_to_matter(const tuya_device_status_t *device_status)
{
    if (!device_status->switch_state) {
        return 0; // kOff
    }
    switch (device_status->ac_mode) {
        case 0: return 1; // kAuto
        case 1: return 3; // kCool
        case 2: return 8; // kDry
        case 3: return 7; // kFanOnly
        case 4: return 4; // kHeat
        default: return 1; // Unknown Tuya mode value -> Auto
    }
}

// Inverse of map_tuya_mode_to_matter. Returns -1 for Matter modes Tuya's "mode"
// DP has no equivalent for (EmergencyHeat, Precooling, Sleep); kOff is handled
// by the caller via tuya_set_power(), not this mapping.
static int8_t map_matter_mode_to_tuya(uint8_t matter_mode)
{
    switch (matter_mode) {
        case 1: return 0; // kAuto -> auto
        case 3: return 1; // kCool -> cool
        case 8: return 2; // kDry -> dry
        case 7: return 3; // kFanOnly -> fan
        case 4: return 4; // kHeat -> heat
        default: return -1;
    }
}

static void apply_status_to_matter(const tuya_device_status_t *device_status)
{
    matter_update_onoff(device_status->switch_state);
    // Mini-split's own indoor reading -- the Thermostat's LocalTemperature.
    // The BME280's independent indoor reading (if fitted) lives on its own
    // Temperature Sensor endpoint instead; see env_task/matter_update_aux_temperature.
    matter_update_local_temperature(device_status->temp_current);
    matter_update_heating_setpoint(device_status->temp_set);
    matter_update_cooling_setpoint(device_status->temp_set);
    matter_update_system_mode(map_tuya_mode_to_matter(device_status));

    uint8_t compressor_pct = compressor_demand_percent(device_status);
    matter_update_compressor_demand(compressor_pct);
    matter_update_compressor_running(compressor_pct > 0);

    // Mini-split's own outdoor ambient reading -- separate endpoint from both
    // indoor temperatures above.
    matter_update_outdoor_temperature(device_status->outdoor_temp);
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
 *
 * Retries indefinitely rather than giving up after a fixed number of
 * attempts. This used to ESP_ERROR_CHECK-abort (full reboot) after 20
 * one-second retries, which -- on Thread, where NTP depends on border
 * routing/DNS64 being fully up -- destroyed freshly-completed Matter/Thread
 * commissioning state on every transient hiccup. A slow or temporarily
 * unreachable NTP server should not cost the device its fabric.
 */
static esp_err_t wait_for_time_sync(void)
{
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;

    ESP_LOGI(TAG, "Starting SNTP time sync...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    while (1) {
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year >= (2024 - 1900)) {
            ESP_LOGI(TAG, "Time synchronized: %s", asctime(&timeinfo));
            return ESP_OK;
        }
        if (retry > 0 && retry % 20 == 0) {
            ESP_LOGW(TAG, "Still waiting for SNTP time sync (%d s)...", retry);
        }
        retry++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
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
        ESP_LOGI(TAG, "  Mode: %s%s", ac_mode_name(device_status.ac_mode),
                 device_status.heat ? " (Aux Heat ON)" : "");
        ESP_LOGI(TAG, "  Compressor: %d%% (%dHz)  Outdoor Temp: %.1f°C",
                 compressor_demand_percent(&device_status),
                 device_status.compressor_frequency,
                 device_status.outdoor_temp / 100.0f);

        // Update cached status and Matter attributes with source-of-truth Tuya status.
        cache_and_apply_status(&device_status);

        g_sync_state.last_status_update = xTaskGetTickCount();
    }
}

/**
 * @brief Command routing task: checks for Matter commands and sends to Tuya
 * 
 * Flow:
 * 1. Check if the controller sent any commands
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
            ESP_LOGI(TAG, "Processing OnOff command from controller: %s",
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
            ESP_LOGI(TAG, "Processing mode command from controller: %u", mode_cmd);

            esp_err_t result;
            if (mode_cmd == 0) {
                // kOff: Tuya's "mode" DP has no "off" value, use the power switch instead.
                result = tuya_set_power(false);
            } else {
                int8_t tuya_mode = map_matter_mode_to_tuya(mode_cmd);
                if (tuya_mode < 0) {
                    ESP_LOGW(TAG, "Matter mode %u has no Tuya equivalent, ignoring", mode_cmd);
                    matter_clear_mode_command();
                    continue;
                }
                result = tuya_set_mode((uint8_t)tuya_mode);
            }

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
 * @brief Environment sensor task: reads BME280 and updates the standalone
 *        Matter temperature + humidity sensor endpoints.
 */
static void env_task(void *param)
{
    ESP_LOGI(TAG, "Environment sensor task started (BME280, interval: %ums)",
             ENV_POLL_INTERVAL_MS);

    while (1) {
        bme280_reading_t reading = {0};
        if (bme280_read(&reading) == ESP_OK) {
            int16_t temp_centi = (int16_t)lroundf(reading.temperature_c * 100.0f);
            uint16_t hum_centi = (uint16_t)lroundf(reading.humidity_pct * 100.0f);
            matter_update_aux_temperature(temp_centi);
            matter_update_aux_humidity(hum_centi);
            ESP_LOGI(TAG, "BME280: %.2f\u00b0C  %.1f%%RH  %.1f hPa",
                     reading.temperature_c, reading.humidity_pct, reading.pressure_hpa);
        } else {
            ESP_LOGW(TAG, "BME280 read failed");
        }

        vTaskDelay(pdMS_TO_TICKS(ENV_POLL_INTERVAL_MS));
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
        ESP_LOGI(TAG, "Network Disconnects: %u", g_sync_state.network_disconnects);
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

    // Initialize TCP/IP stack before Matter brings up its own (Thread) netif.
    ESP_ERROR_CHECK(esp_netif_init());

    // Initialize event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize Matter device and start commissioning. Matter now owns
    // network bring-up (Thread) as part of its normal commissioning flow,
    // rather than the app pre-connecting with baked-in credentials first --
    // see matter_set_network_event_group()/app_chip_event_handler() in
    // matter_device.cpp for how connectivity is signaled back here.
    ESP_LOGI(TAG, "Initializing Matter device...");
    ESP_ERROR_CHECK(matter_device_init());

    matter_set_network_event_group(g_app_event_group, NETWORK_CONNECTED_BIT);

    ESP_LOGI(TAG, "Starting Matter commissioning...");
    ESP_ERROR_CHECK(matter_start_commissioning());

    // Wait for network connectivity before starting network-dependent
    // services. An already-commissioned device reattaches to its stored
    // Thread network within seconds; an uncommissioned device waits here
    // indefinitely for the user to commission it (e.g. via Home Assistant).
    ESP_LOGI(TAG, "Waiting for network connectivity (commission via Home Assistant if not already paired)...");
    EventBits_t bits = 0;
    while (!(bits & NETWORK_CONNECTED_BIT)) {
        bits = xEventGroupWaitBits(g_app_event_group,
                                   NETWORK_CONNECTED_BIT,
                                   pdFALSE,
                                   pdFALSE,
                                   pdMS_TO_TICKS(30000));
        if (!(bits & NETWORK_CONNECTED_BIT)) {
            ESP_LOGI(TAG, "Still waiting for network connectivity...");
        }
    }
    ESP_LOGI(TAG, "Network connectivity established");

    // Tuya authentication requires valid system time. Retries indefinitely
    // rather than aborting -- see wait_for_time_sync() for why.
    wait_for_time_sync();

    // Initialize Tuya client
    ESP_LOGI(TAG, "Initializing Tuya client...");
    ESP_ERROR_CHECK(tuya_client_init(
        TUYA_DEVICE_ID,
        TUYA_CLIENT_ID,
        TUYA_CLIENT_SECRET
    ));

    // Initialize optional BME280 environment sensor (temperature + humidity).
    // If absent, the aux temperature endpoint falls back to the Tuya indoor temp.
    if (bme280_init() == ESP_OK) {
        xTaskCreate(env_task,
                    "env_sensor",
                    3072,
                    NULL,
                    2,
                    NULL);
    } else {
        ESP_LOGW(TAG, "BME280 not detected; humidity endpoint inactive, aux temp mirrors Tuya");
    }
    
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
