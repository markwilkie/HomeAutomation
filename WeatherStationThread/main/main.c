// Weather station (Thread rewrite) -- M1 scaffold: boot banner, then blink the
// status LED and log a heartbeat. PLAN.md milestones add the sensors (M2),
// LP-core pulse counting (M3), Thread transport (M4), sleep (M5) and OTA (M6).
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "board.h"
#include "status_led.h"

static const char *TAG = "ws";

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_USB:       return "USB";
    case ESP_RST_JTAG:      return "JTAG";
    default:                return "other";
    }
}

void app_main(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BASE);
    ESP_LOGI(TAG, "Weather station %s on %s", esp_app_get_description()->version, BOARD_NAME);
    ESP_LOGI(TAG, "MAC %02X:%02X:%02X:%02X:%02X:%02X, reset reason: %s",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             reset_reason_str(esp_reset_reason()));

    status_led_init();
    for (uint32_t n = 0;; n++) {
        status_led_set(true);
        vTaskDelay(pdMS_TO_TICKS(100));
        status_led_set(false);
        vTaskDelay(pdMS_TO_TICKS(900));
        if (n % 10 == 0) {
            ESP_LOGI(TAG, "alive, uptime %" PRId64 " s", esp_timer_get_time() / 1000000);
        }
    }
}
