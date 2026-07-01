// Dormer Window State
//
// Matter Contact Sensor (device type 0x0015) for ESP32-C6. A single digital
// GPIO reads a reed switch on the window; the debounced state is exposed to
// any Matter controller (Apple Home, Google Home, SmartThings, etc.) via the
// Boolean State cluster's StateValue attribute.

#include <esp_log.h>
#include <esp_matter.h>
#include <nvs_flash.h>

#if CONFIG_ENABLE_CHIP_SHELL
#include <esp_matter_console.h>
#endif

#include "window_sensor.h"

using namespace chip::app::Clusters;
using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;

static const char *TAG = "app_main";

static uint16_t window_endpoint_id = 0;

// Local stand-in for the ABORT_APP_ON_FAILURE helper used by the upstream
// esp-matter examples (defined in examples/common/utils, which this
// standalone project doesn't pull in). Logs and aborts on failure.
#define ABORT_APP_ON_FAILURE(condition, ...)                                                                         \
    do {                                                                                                              \
        if (!(condition)) {                                                                                          \
            __VA_ARGS__;                                                                                              \
            abort();                                                                                                  \
        }                                                                                                             \
    } while (0)

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP address changed");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "Fabric removed");
        break;
    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                        uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: endpoint %u, effect %u", endpoint_id, effect_id);
    return ESP_OK;
}

// Boolean State is read-only from a controller's perspective (the sensor is
// the source of truth), so there's nothing to act on for incoming writes.
// Kept as a no-op hook for future attributes (e.g. battery level).
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                          uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    return ESP_OK;
}

extern "C" void app_main()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Configure the GPIO and take an initial reading before the endpoint is
    // created, so the Matter attribute starts out correct rather than at a
    // default value.
    app_driver_gpio_init();

    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    contact_sensor::config_t contact_sensor_config;
    contact_sensor_config.boolean_state.state_value = app_driver_window_is_open();

    endpoint_t *endpoint = contact_sensor::create(node, &contact_sensor_config, ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create contact sensor endpoint"));

    window_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Window contact sensor created on endpoint %u", window_endpoint_id);

    // Now that the endpoint exists, start the interrupt-driven driver that
    // keeps it in sync with the GPIO going forward.
    app_driver_init(window_endpoint_id);

    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::init();
#endif
}
