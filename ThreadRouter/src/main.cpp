/**
 * @file main.cpp
 * @brief Minimal Matter-over-Thread endpoint.
 *
 * Sole purpose: join the home Thread mesh as a Full Thread Device (router),
 * extending Thread coverage/reception for other Thread devices (MiniSplit's
 * board has repeatedly fought a marginal link -- see its matter_device.cpp
 * thread_rssi endpoint). Exposes one On/Off Plug-in Unit endpoint purely so
 * it commissions as a normal Matter node; the endpoint has no real-world
 * effect, and mesh routing happens automatically underneath once attached.
 *
 * Commissioning gotcha: see ../CLAUDE.md "How to commission a Thread device
 * in this house" -- the phone must prefer OTBR's network, not the
 * SmartThings Hub's, or the device lands on the wrong Thread network.
 */

#include <cstring>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_attribute_utils.h>
#include <esp_matter_endpoint.h>

#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include "esp_openthread_types.h"
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

using namespace esp_matter;

static const char *TAG = "THREAD_ROUTER";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== Thread Router starting ===");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    node::config_t node_cfg;
    strncpy(node_cfg.root_node.basic_information.node_label, "Thread Router",
            sizeof(node_cfg.root_node.basic_information.node_label) - 1);
    node_t *node = node::create(&node_cfg, nullptr, nullptr);
    if (!node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return;
    }

    endpoint::on_off_plug_in_unit::config_t switch_cfg;
    switch_cfg.on_off.on_off = false;
    // Null = no defined startup behavior (don't force on/off on every boot).
    switch_cfg.on_off_lighting.start_up_on_off = nullable<uint8_t>();
    endpoint_t *endpoint = endpoint::on_off_plug_in_unit::create(node, &switch_cfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!endpoint) {
        ESP_LOGE(TAG, "Failed to create endpoint");
        return;
    }

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    // esp_matter::start() initializes the Thread stack internally but
    // asserts on a platform config the app must supply first -- esp_matter
    // has no default of its own. ESP32-C6 has a native 802.15.4 radio (no
    // RCP/UART link needed) and no separate CLI host.
    esp_openthread_platform_config_t ot_config = {};
    ot_config.radio_config.radio_mode = RADIO_MODE_NATIVE;
    ot_config.host_config.host_connection_mode = HOST_CONNECTION_MODE_NONE;
    ot_config.port_config.storage_partition_name = "nvs";
    ot_config.port_config.netif_queue_size = 10;
    ot_config.port_config.task_queue_size = 10;
    set_openthread_platform_config(&ot_config);
#endif

    esp_err_t err = esp_matter::start(nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter: %s", esp_err_to_name(err));
        return;
    }

    chip::RendezvousInformationFlags rendezvous_flags(chip::RendezvousInformationFlag::kBLE);
    PrintOnboardingCodes(rendezvous_flags);
    ESP_LOGI(TAG, "=== Ready -- commission via Home Assistant (BLE) ===");
}
