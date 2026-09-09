#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "matter_device.h"

static const char *TAG = "MAIN";

// Set once Matter's network layer (Thread) reports connectivity -- see
// matter_set_network_event_group() / app_chip_event_handler() in
// matter_device.cpp.
#define NETWORK_CONNECTED_BIT BIT0

static EventGroupHandle_t g_app_event_group = NULL;

// Milestone 3: just log pending Matter commands so they're visible on the
// serial console while commissioning/testing from a controller. Milestone 3
// step 3 wires these into real IR sends via the shadow-state model described
// in ../instructions.txt; nothing here actually drives the AC yet.
static void command_task(void *pvParameters)
{
    (void)pvParameters;
    for (;;) {
        if (matter_get_system_mode_command_pending()) {
            ESP_LOGI(TAG, "Pending command: SystemMode=%u", matter_get_system_mode());
            matter_clear_system_mode_command();
        }
        if (matter_get_cooling_setpoint_command_pending()) {
            ESP_LOGI(TAG, "Pending command: CoolingSetpoint=%d (0.01C)", matter_get_cooling_setpoint());
            matter_clear_cooling_setpoint_command();
        }
        if (matter_get_heating_setpoint_command_pending()) {
            ESP_LOGI(TAG, "Pending command: HeatingSetpoint=%d (0.01C)", matter_get_heating_setpoint());
            matter_clear_heating_setpoint_command();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "\n\n=== MiniSplit IR Bridge - Device B (Thermostat skeleton) ===\n");

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
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "Initializing Matter device...");
    ESP_ERROR_CHECK(matter_device_init());

    matter_set_network_event_group(g_app_event_group, NETWORK_CONNECTED_BIT);

    ESP_LOGI(TAG, "Starting Matter commissioning...");
    ESP_ERROR_CHECK(matter_start_commissioning());

    ESP_LOGI(TAG, "Waiting for network connectivity (commission via a Matter controller if not already paired)...");
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

    xTaskCreate(command_task, "command_task", 4096, NULL, 5, NULL);
}
