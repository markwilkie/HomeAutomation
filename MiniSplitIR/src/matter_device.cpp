/**
 * @file matter_device.cpp
 * @brief Matter Thermostat endpoint for the IR follow-me bridge (Device B)
 *
 * Milestone 3 skeleton: a single Thermostat endpoint that commissions onto
 * the Thread fabric and accepts SystemMode/setpoint writes from a
 * controller. No IR send logic yet (Milestone 3's own next step) and no
 * bound sensor yet (Milestone 5) -- LocalTemperature just sits at its null
 * "unknown" boot value until matter_update_local_temperature() is wired to
 * something real.
 */

#include "matter_device.h"

#include "freertos/task.h"
#include "esp_log.h"
#include "esp_matter.h"
#include "esp_matter_attribute_utils.h"
#include "esp_matter_endpoint.h"

#include <app-common/zap-generated/cluster-objects.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <platform/CHIPDeviceLayer.h>
#include <cstring>
#include <inttypes.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include "esp_openthread_types.h"
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

using namespace esp_matter;
using namespace chip::app::Clusters;

static const char *TAG = "MATTER_DEVICE";

// Set via matter_set_network_event_group(); signaled by app_chip_event_handler()
// as Matter's own network layer (Thread) attaches/detaches -- see
// ../../MiniSplit/src/matter_device.cpp for the same pattern and its
// reasoning (kThreadConnectivityChange fires reliably and immediately;
// kInternetConnectivityChange does not).
static EventGroupHandle_t g_network_event_group = nullptr;
static EventBits_t g_network_connected_bit = 0;

static void app_chip_event_handler(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t /*arg*/)
{
    if (!g_network_event_group) {
        return;
    }

    if (event->Type == chip::DeviceLayer::DeviceEventType::kThreadConnectivityChange) {
        if (event->ThreadConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Established) {
            ESP_LOGI(TAG, "Thread connectivity established");
            xEventGroupSetBits(g_network_event_group, g_network_connected_bit);
        } else if (event->ThreadConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Lost) {
            ESP_LOGW(TAG, "Thread connectivity lost");
            xEventGroupClearBits(g_network_event_group, g_network_connected_bit);
        }
    }
}

extern "C" void matter_set_network_event_group(EventGroupHandle_t event_group, EventBits_t connected_bit)
{
    g_network_event_group = event_group;
    g_network_connected_bit = connected_bit;
}

typedef struct {
    uint8_t system_mode;
    int16_t local_temperature; // mirrors g_local_temperature_known below
    int16_t heating_setpoint;
    int16_t cooling_setpoint;
    bool system_mode_command_pending;
    bool heating_setpoint_command_pending;
    bool cooling_setpoint_command_pending;
} matter_device_state_t;

static matter_device_state_t g_matter_state = {
    .system_mode = 0, // kOff
    .local_temperature = 0,
    .heating_setpoint = 2000, // 20.00C
    .cooling_setpoint = 2400, // 24.00C
    .system_mode_command_pending = false,
    .heating_setpoint_command_pending = false,
    .cooling_setpoint_command_pending = false,
};

static bool g_local_temperature_known = false;

static node_t *g_node = nullptr;
static endpoint_t *g_endpoint = nullptr;
static uint16_t g_endpoint_id = 0;
static bool g_internal_attr_update = false;
static bool g_started = false;

static esp_err_t update_attr(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t val)
{
    if (!g_started || !g_endpoint || !g_endpoint_id) {
        return ESP_ERR_INVALID_STATE;
    }

    cluster_t *target_cluster = cluster::get(g_endpoint, cluster_id);
    if (!target_cluster) {
        return ESP_ERR_NOT_FOUND;
    }
    attribute_t *target_attribute = attribute::get(target_cluster, attribute_id);
    if (!target_attribute) {
        return ESP_ERR_NOT_FOUND;
    }

    g_internal_attr_update = true;
    esp_err_t err = attribute::update(g_endpoint_id, cluster_id, attribute_id, &val);
    g_internal_attr_update = false;

    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Attribute update failed (cluster=0x%08" PRIx32 " attr=0x%08" PRIx32 "): %s",
                 cluster_id, attribute_id, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t matter_attribute_callback(attribute::callback_type_t type,
                                           uint16_t endpoint_id,
                                           uint32_t cluster_id,
                                           uint32_t attribute_id,
                                           esp_matter_attr_val_t *val,
                                           void *priv_data)
{
    (void)priv_data;

    if (!val || endpoint_id != g_endpoint_id || g_internal_attr_update) {
        return ESP_OK;
    }
    if (type != attribute::PRE_UPDATE) {
        return ESP_OK;
    }
    if (cluster_id != Thermostat::Id) {
        return ESP_OK;
    }

    if (attribute_id == Thermostat::Attributes::SystemMode::Id) {
        g_matter_state.system_mode = val->val.u8;
        g_matter_state.system_mode_command_pending = true;
        ESP_LOGI(TAG, "SystemMode command: %u", g_matter_state.system_mode);
        return ESP_OK;
    }
    if (attribute_id == Thermostat::Attributes::OccupiedHeatingSetpoint::Id) {
        g_matter_state.heating_setpoint = val->val.i16;
        g_matter_state.heating_setpoint_command_pending = true;
        ESP_LOGI(TAG, "OccupiedHeatingSetpoint command: %d (0.01C)", g_matter_state.heating_setpoint);
        return ESP_OK;
    }
    if (attribute_id == Thermostat::Attributes::OccupiedCoolingSetpoint::Id) {
        g_matter_state.cooling_setpoint = val->val.i16;
        g_matter_state.cooling_setpoint_command_pending = true;
        ESP_LOGI(TAG, "OccupiedCoolingSetpoint command: %d (0.01C)", g_matter_state.cooling_setpoint);
        return ESP_OK;
    }

    return ESP_OK;
}

extern "C" esp_err_t matter_device_init(void)
{
    ESP_LOGI(TAG, "Initializing Matter device...");

    node::config_t node_cfg;
    strncpy(node_cfg.root_node.basic_information.node_label, "MiniSplit IR Thermostat",
            sizeof(node_cfg.root_node.basic_information.node_label) - 1);
    g_node = node::create(&node_cfg, matter_attribute_callback, nullptr);
    if (!g_node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return ESP_FAIL;
    }

    // Seed real initial values at creation time (not just via update_attr()
    // later, which no-ops until g_started) -- see ../../MiniSplit's
    // matter_device.cpp comment on why a commissioner's first read otherwise
    // sees esp_matter's built-in SystemMode=Auto default, which this
    // endpoint never advertises support for (a spec violation a controller
    // can reasonably refuse to act on).
    endpoint::thermostat::config_t endpoint_cfg;
    endpoint_cfg.thermostat.feature_flags =
        cluster::thermostat::feature::heating::get_id() |
        cluster::thermostat::feature::cooling::get_id();
    endpoint_cfg.thermostat.system_mode = g_matter_state.system_mode;
    // Null/unknown until a real reading exists (Milestone 5's Device A
    // binding) -- see ../../MiniSplit's temperature sensor endpoints for the
    // same "don't show a fake 0.0C after every boot" reasoning.
    endpoint_cfg.thermostat.local_temperature = nullable<int16_t>();
    endpoint_cfg.thermostat.features.heating.occupied_heating_setpoint = g_matter_state.heating_setpoint;
    endpoint_cfg.thermostat.features.cooling.occupied_cooling_setpoint = g_matter_state.cooling_setpoint;
    g_endpoint = endpoint::thermostat::create(g_node, &endpoint_cfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!g_endpoint) {
        ESP_LOGE(TAG, "Failed to create Thermostat endpoint");
        return ESP_FAIL;
    }

    g_endpoint_id = endpoint::get_id(g_endpoint);
    if (!g_endpoint_id) {
        ESP_LOGE(TAG, "Failed to resolve Thermostat endpoint id");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Matter device initialized: thermostat_ep=%u", g_endpoint_id);
    return ESP_OK;
}

extern "C" esp_err_t matter_start_commissioning(void)
{
    if (!g_node) {
        ESP_LOGE(TAG, "Matter node not initialized");
        return ESP_FAIL;
    }

    if (esp_matter::is_started()) {
        ESP_LOGI(TAG, "Matter already started");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting Matter commissioning...");

    CHIP_ERROR handler_err = chip::DeviceLayer::PlatformMgr().AddEventHandler(app_chip_event_handler, 0);
    if (handler_err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Failed to register CHIP event handler: %" CHIP_ERROR_FORMAT, handler_err.Format());
    }

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    // ESP32-C6 has a native 802.15.4 radio -- no RCP/UART link needed, and
    // this device has no separate CLI host, hence HOST_CONNECTION_MODE_NONE.
    // Same as ../../MiniSplit/src/matter_device.cpp.
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
        return err;
    }

    g_started = true;

    chip::RendezvousInformationFlags rendezvous_flags(chip::RendezvousInformationFlag::kBLE);
    PrintOnboardingCodes(rendezvous_flags);
    ESP_LOGI(TAG, "Matter commissioning started");
    return ESP_OK;
}

extern "C" void matter_update_local_temperature(int16_t temp_c)
{
    g_matter_state.local_temperature = temp_c;
    g_local_temperature_known = true;
    update_attr(Thermostat::Id, Thermostat::Attributes::LocalTemperature::Id,
                esp_matter_nullable_int16(nullable<int16_t>(temp_c)));
}

extern "C" bool matter_get_system_mode_command_pending(void)
{
    return g_matter_state.system_mode_command_pending;
}

extern "C" uint8_t matter_get_system_mode(void)
{
    return g_matter_state.system_mode;
}

extern "C" void matter_clear_system_mode_command(void)
{
    g_matter_state.system_mode_command_pending = false;
}

extern "C" bool matter_get_cooling_setpoint_command_pending(void)
{
    return g_matter_state.cooling_setpoint_command_pending;
}

extern "C" int16_t matter_get_cooling_setpoint(void)
{
    return g_matter_state.cooling_setpoint;
}

extern "C" void matter_clear_cooling_setpoint_command(void)
{
    g_matter_state.cooling_setpoint_command_pending = false;
}

extern "C" bool matter_get_heating_setpoint_command_pending(void)
{
    return g_matter_state.heating_setpoint_command_pending;
}

extern "C" int16_t matter_get_heating_setpoint(void)
{
    return g_matter_state.heating_setpoint;
}

extern "C" void matter_clear_heating_setpoint_command(void)
{
    g_matter_state.heating_setpoint_command_pending = false;
}
