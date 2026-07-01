// Digital-pin window contact driver.
//
// Reads a reed switch / magnetic contact sensor on a single GPIO, debounces
// it in software, and pushes confirmed state changes into the Matter
// Boolean State cluster (StateValue attribute) on the device's
// Contact Sensor endpoint.

#include "window_sensor.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "sdkconfig.h"

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "window_sensor";

static const gpio_num_t WINDOW_GPIO = static_cast<gpio_num_t>(CONFIG_WINDOW_SENSOR_GPIO);
static const bool ACTIVE_LOW = CONFIG_WINDOW_SENSOR_ACTIVE_LOW;
static const uint32_t DEBOUNCE_MS = CONFIG_WINDOW_SENSOR_DEBOUNCE_MS;

static uint16_t s_endpoint_id = 0;
static TaskHandle_t s_debounce_task = nullptr;
static volatile bool s_last_reported_open = false;

// Translate the raw pin level into a logical "window is open" boolean,
// accounting for the configured wiring polarity.
static inline bool raw_pin_is_open(void)
{
    int level = gpio_get_level(WINDOW_GPIO);
    return ACTIVE_LOW ? (level == 1) : (level == 0);
}

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_debounce_task, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

// Woken on every edge; waits out the debounce window, re-checks the pin,
// and only then reports a change to Matter. This intentionally runs in a
// normal task (not the ISR) so it is safe to call Matter APIs.
static void debounce_task(void *arg)
{
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));

        bool is_open = raw_pin_is_open();
        if (is_open == s_last_reported_open) {
            continue;
        }
        s_last_reported_open = is_open;

        ESP_LOGI(TAG, "Window %s", is_open ? "OPEN" : "CLOSED");

        if (s_endpoint_id == 0) {
            continue;
        }

        esp_matter_attr_val_t val = esp_matter_bool(is_open);
        attribute::update(s_endpoint_id, BooleanState::Id, BooleanState::Attributes::StateValue::Id, &val);
    }
}

void app_driver_gpio_init(void)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << WINDOW_GPIO);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = ACTIVE_LOW ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = ACTIVE_LOW ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE;
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    s_last_reported_open = raw_pin_is_open();
    ESP_LOGI(TAG, "GPIO%d configured, initial state: %s", WINDOW_GPIO, s_last_reported_open ? "OPEN" : "CLOSED");
}

bool app_driver_window_is_open(void)
{
    return raw_pin_is_open();
}

void app_driver_init(uint16_t endpoint_id)
{
    s_endpoint_id = endpoint_id;

    xTaskCreate(debounce_task, "window_debounce", 3072, nullptr, 5, &s_debounce_task);

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(WINDOW_GPIO, gpio_isr_handler, nullptr));
}
