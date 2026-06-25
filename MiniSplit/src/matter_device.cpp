/**
 * @file matter_device.cpp
 * @brief Matter device implementation for mini-split AC control
 */

#include "matter_device.h"

#include "esp_log.h"
#include "esp_matter.h"
#include "esp_matter_attribute_utils.h"
#include "esp_matter_endpoint.h"

#include <app-common/zap-generated/cluster-objects.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <inttypes.h>
#include <limits.h>

using namespace esp_matter;
using namespace chip::app::Clusters;

static const char *TAG = "MATTER_DEVICE";

// Global state for Matter attributes
typedef struct {
    bool onoff;
    bool light;
    bool beep;
    int16_t current_temp;
    int16_t heating_setpoint;
    int16_t cooling_setpoint;
    uint8_t system_mode;
    bool mode_auto;
    bool mode_eco;
    bool mode_dry;
    bool heat;
    bool onoff_command_pending;
    bool light_command_pending;
    bool beep_command_pending;
    int16_t heating_setpoint_command_pending;
    int16_t cooling_setpoint_command_pending;
    uint8_t mode_command_pending;
} matter_device_state_t;

static matter_device_state_t g_matter_state = {
    .onoff = false,
    .light = false,
    .beep = false,
    .current_temp = 2200,
    .heating_setpoint = 2000,
    .cooling_setpoint = 2400,
    .system_mode = 3,
    .mode_auto = true,
    .mode_eco = false,
    .mode_dry = false,
    .heat = false,
    .onoff_command_pending = false,
    .light_command_pending = false,
    .beep_command_pending = false,
    .heating_setpoint_command_pending = INT16_MIN,
    .cooling_setpoint_command_pending = INT16_MIN,
    .mode_command_pending = 0xFF,
};

static node_t *g_node = nullptr;
static endpoint_t *g_endpoint = nullptr;
static endpoint_t *g_light_endpoint = nullptr;
static endpoint_t *g_beep_endpoint = nullptr;
static uint16_t g_endpoint_id = 0;
static uint16_t g_light_endpoint_id = 0;
static uint16_t g_beep_endpoint_id = 0;
static bool g_internal_attr_update = false;
static bool g_started = false;

static esp_err_t update_attr_on_endpoint(endpoint_t *target_endpoint,
                                         uint16_t target_endpoint_id,
                                         uint32_t cluster_id,
                                         uint32_t attribute_id,
                                         esp_matter_attr_val_t val)
{
    if (!g_started || !target_endpoint || !target_endpoint_id) {
        return ESP_ERR_INVALID_STATE;
    }

    // Some mapped datapoints do not exist on all endpoint types; skip gracefully.
    cluster_t *target_cluster = cluster::get(target_endpoint, cluster_id);
    if (!target_cluster) {
        return ESP_ERR_NOT_FOUND;
    }
    attribute_t *target_attribute = attribute::get(target_cluster, attribute_id);
    if (!target_attribute) {
        return ESP_ERR_NOT_FOUND;
    }

    g_internal_attr_update = true;
    esp_err_t err = attribute::update(target_endpoint_id, cluster_id, attribute_id, &val);
    g_internal_attr_update = false;

    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Attribute update failed (ep=%u cluster=0x%08" PRIx32 " attr=0x%08" PRIx32 "): %s",
                 target_endpoint_id, cluster_id, attribute_id, esp_err_to_name(err));
    }

    return err;
}

static esp_err_t update_attr(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t val)
{
    return update_attr_on_endpoint(g_endpoint, g_endpoint_id, cluster_id, attribute_id, val);
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

    if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
        if (endpoint_id == g_endpoint_id) {
            g_matter_state.onoff = val->val.b;
            g_matter_state.onoff_command_pending = true;
            ESP_LOGI(TAG, "OnOff command from Matter: %s", g_matter_state.onoff ? "ON" : "OFF");
        } else if (endpoint_id == g_light_endpoint_id) {
            g_matter_state.light = val->val.b;
            g_matter_state.light_command_pending = true;
            ESP_LOGI(TAG, "Light command from Matter: %s", g_matter_state.light ? "ON" : "OFF");
        } else if (endpoint_id == g_beep_endpoint_id) {
            g_matter_state.beep = val->val.b;
            g_matter_state.beep_command_pending = true;
            ESP_LOGI(TAG, "Beep command from Matter: %s", g_matter_state.beep ? "ON" : "OFF");
        }
        return ESP_OK;
    }

    if (cluster_id == Thermostat::Id) {
        if (attribute_id == Thermostat::Attributes::OccupiedHeatingSetpoint::Id) {
            g_matter_state.heating_setpoint = val->val.i16;
            g_matter_state.heating_setpoint_command_pending = g_matter_state.heating_setpoint;
            ESP_LOGI(TAG, "Heating setpoint command: %d", g_matter_state.heating_setpoint);
        } else if (attribute_id == Thermostat::Attributes::OccupiedCoolingSetpoint::Id) {
            g_matter_state.cooling_setpoint = val->val.i16;
            g_matter_state.cooling_setpoint_command_pending = g_matter_state.cooling_setpoint;
            ESP_LOGI(TAG, "Cooling setpoint command: %d", g_matter_state.cooling_setpoint);
        } else if (attribute_id == Thermostat::Attributes::SystemMode::Id) {
            g_matter_state.system_mode = val->val.u8;
            g_matter_state.mode_command_pending = g_matter_state.system_mode;
            ESP_LOGI(TAG, "System mode command: %u", g_matter_state.system_mode);
        }
    }

    return ESP_OK;
}

extern "C" esp_err_t matter_device_init(void)
{
    ESP_LOGI(TAG, "Initializing Matter device...");

    node::config_t node_cfg;
    g_node = node::create(&node_cfg, matter_attribute_callback, nullptr);
    if (!g_node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return ESP_FAIL;
    }

    endpoint::thermostat::config_t endpoint_cfg;
    endpoint_cfg.thermostat.feature_flags =
        cluster::thermostat::feature::heating::get_id() |
        cluster::thermostat::feature::cooling::get_id();
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

    endpoint::on_off_light::config_t light_cfg;
    g_light_endpoint = endpoint::on_off_light::create(g_node, &light_cfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!g_light_endpoint) {
        ESP_LOGE(TAG, "Failed to create Light endpoint");
        return ESP_FAIL;
    }
    g_light_endpoint_id = endpoint::get_id(g_light_endpoint);
    if (!g_light_endpoint_id) {
        ESP_LOGE(TAG, "Failed to resolve Light endpoint id");
        return ESP_FAIL;
    }

    endpoint::on_off_light::config_t beep_cfg;
    g_beep_endpoint = endpoint::on_off_light::create(g_node, &beep_cfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!g_beep_endpoint) {
        ESP_LOGE(TAG, "Failed to create Beep endpoint");
        return ESP_FAIL;
    }
    g_beep_endpoint_id = endpoint::get_id(g_beep_endpoint);
    if (!g_beep_endpoint_id) {
        ESP_LOGE(TAG, "Failed to resolve Beep endpoint id");
        return ESP_FAIL;
    }

    update_attr(OnOff::Id, OnOff::Attributes::OnOff::Id, esp_matter_bool(g_matter_state.onoff));
    update_attr(Thermostat::Id, Thermostat::Attributes::LocalTemperature::Id,
                esp_matter_nullable_int16(nullable<int16_t>(g_matter_state.current_temp)));
    update_attr(Thermostat::Id, Thermostat::Attributes::OccupiedHeatingSetpoint::Id,
                esp_matter_int16(g_matter_state.heating_setpoint));
    update_attr(Thermostat::Id, Thermostat::Attributes::OccupiedCoolingSetpoint::Id,
                esp_matter_int16(g_matter_state.cooling_setpoint));
    update_attr(Thermostat::Id, Thermostat::Attributes::SystemMode::Id,
                esp_matter_enum8(g_matter_state.system_mode));
    update_attr_on_endpoint(g_light_endpoint, g_light_endpoint_id,
                            OnOff::Id, OnOff::Attributes::OnOff::Id,
                            esp_matter_bool(g_matter_state.light));
    update_attr_on_endpoint(g_beep_endpoint, g_beep_endpoint_id,
                            OnOff::Id, OnOff::Attributes::OnOff::Id,
                            esp_matter_bool(g_matter_state.beep));

    ESP_LOGI(TAG, "Matter device initialized: thermostat_ep=%u light_ep=%u beep_ep=%u",
             g_endpoint_id, g_light_endpoint_id, g_beep_endpoint_id);
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

extern "C" void matter_update_onoff(bool on_off)
{
    if (g_matter_state.onoff == on_off) {
        return;
    }
    g_matter_state.onoff = on_off;
    update_attr(OnOff::Id, OnOff::Attributes::OnOff::Id, esp_matter_bool(on_off));
}

extern "C" void matter_update_local_temperature(int16_t temp_c)
{
    g_matter_state.current_temp = temp_c;
    update_attr(Thermostat::Id, Thermostat::Attributes::LocalTemperature::Id,
                esp_matter_nullable_int16(nullable<int16_t>(temp_c)));
}

extern "C" void matter_update_heating_setpoint(int16_t temp_c)
{
    g_matter_state.heating_setpoint = temp_c;
    update_attr(Thermostat::Id, Thermostat::Attributes::OccupiedHeatingSetpoint::Id, esp_matter_int16(temp_c));
}

extern "C" void matter_update_cooling_setpoint(int16_t temp_c)
{
    g_matter_state.cooling_setpoint = temp_c;
    update_attr(Thermostat::Id, Thermostat::Attributes::OccupiedCoolingSetpoint::Id, esp_matter_int16(temp_c));
}

extern "C" void matter_update_system_mode(uint8_t mode)
{
    g_matter_state.system_mode = mode;
    update_attr(Thermostat::Id, Thermostat::Attributes::SystemMode::Id, esp_matter_enum8(mode));
}

extern "C" void matter_update_mode_flags(bool mode_auto, bool mode_eco, bool mode_dry, bool heat)
{
    g_matter_state.mode_auto = mode_auto;
    g_matter_state.mode_eco = mode_eco;
    g_matter_state.mode_dry = mode_dry;
    g_matter_state.heat = heat;
}

extern "C" void matter_update_light(bool on_off)
{
    if (g_matter_state.light == on_off) {
        return;
    }
    g_matter_state.light = on_off;
    update_attr_on_endpoint(g_light_endpoint, g_light_endpoint_id,
                            OnOff::Id, OnOff::Attributes::OnOff::Id,
                            esp_matter_bool(on_off));
}

extern "C" void matter_update_beep(bool on_off)
{
    if (g_matter_state.beep == on_off) {
        return;
    }
    g_matter_state.beep = on_off;
    update_attr_on_endpoint(g_beep_endpoint, g_beep_endpoint_id,
                            OnOff::Id, OnOff::Attributes::OnOff::Id,
                            esp_matter_bool(on_off));
}

extern "C" bool matter_get_onoff_command(void)
{
    return g_matter_state.onoff_command_pending;
}

extern "C" bool matter_get_onoff_state(void)
{
    return g_matter_state.onoff;
}

extern "C" int16_t matter_get_heating_setpoint_command(void)
{
    if (g_matter_state.heating_setpoint_command_pending == INT16_MIN) {
        return -1;
    }
    return g_matter_state.heating_setpoint_command_pending;
}

extern "C" int16_t matter_get_cooling_setpoint_command(void)
{
    if (g_matter_state.cooling_setpoint_command_pending == INT16_MIN) {
        return -1;
    }
    return g_matter_state.cooling_setpoint_command_pending;
}

extern "C" uint8_t matter_get_system_mode_command(void)
{
    return g_matter_state.mode_command_pending;
}

extern "C" bool matter_get_light_command(void)
{
    return g_matter_state.light_command_pending;
}

extern "C" bool matter_get_light_state(void)
{
    return g_matter_state.light;
}

extern "C" bool matter_get_beep_command(void)
{
    return g_matter_state.beep_command_pending;
}

extern "C" bool matter_get_beep_state(void)
{
    return g_matter_state.beep;
}

extern "C" void matter_clear_onoff_command(void)
{
    g_matter_state.onoff_command_pending = false;
}

extern "C" void matter_clear_setpoint_command(void)
{
    g_matter_state.heating_setpoint_command_pending = INT16_MIN;
    g_matter_state.cooling_setpoint_command_pending = INT16_MIN;
}

extern "C" void matter_clear_heating_setpoint_command(void)
{
    g_matter_state.heating_setpoint_command_pending = INT16_MIN;
}

extern "C" void matter_clear_cooling_setpoint_command(void)
{
    g_matter_state.cooling_setpoint_command_pending = INT16_MIN;
}

extern "C" void matter_clear_mode_command(void)
{
    g_matter_state.mode_command_pending = 0xFF;
}

extern "C" void matter_clear_light_command(void)
{
    g_matter_state.light_command_pending = false;
}

extern "C" void matter_clear_beep_command(void)
{
    g_matter_state.beep_command_pending = false;
}

extern "C" void matter_device_deinit(void)
{
    g_node = nullptr;
    g_endpoint = nullptr;
    g_light_endpoint = nullptr;
    g_beep_endpoint = nullptr;
    g_endpoint_id = 0;
    g_light_endpoint_id = 0;
    g_beep_endpoint_id = 0;
    g_started = false;
    ESP_LOGI(TAG, "Matter device deinitialized");
}
