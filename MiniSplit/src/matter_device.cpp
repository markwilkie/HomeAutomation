/**
 * @file matter_device.cpp
 * @brief Matter device implementation for mini-split AC control
 */

#include "matter_device.h"

#include "esp_log.h"
#include "esp_matter.h"
#include "esp_matter_attribute_utils.h"
#include "esp_matter_endpoint.h"
#include "nvs.h"

#include <app-common/zap-generated/cluster-objects.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <platform/CHIPDeviceLayer.h>
#include <cstring>
#include <inttypes.h>
#include <limits.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include "esp_openthread_types.h"
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

using namespace esp_matter;
using namespace chip::app::Clusters;

static const char *TAG = "MATTER_DEVICE";

// Set via matter_set_network_event_group(); signaled by app_chip_event_handler()
// below as Matter's own network layer (Thread) attaches/detaches. Network
// provisioning is now owned by Matter's commissioning flow rather than
// app-level pre-connect code, so this is how main.c learns connectivity state.
static EventGroupHandle_t g_network_event_group = nullptr;
static EventBits_t g_network_connected_bit = 0;

static void app_chip_event_handler(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t /*arg*/)
{
    if (!g_network_event_group) {
        return;
    }

    // kInternetConnectivityChange (actual WAN reachability) is the semantically
    // "correct" signal, but on real hardware it either fires much later than
    // Thread mesh attachment or may not fire reliably at all -- the SDK's own
    // header admits this event "does not actually validate that the actual
    // internet is reachable" either, so it isn't even a stronger guarantee.
    // kThreadConnectivityChange (Thread interface up) fires immediately once
    // the device attaches to the mesh, which is what's actually needed to
    // start talking to Tuya's cloud via OTBR's border routing. Treat either as
    // sufficient.
    if (event->Type == chip::DeviceLayer::DeviceEventType::kThreadConnectivityChange) {
        if (event->ThreadConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Established) {
            ESP_LOGI(TAG, "Thread connectivity established");
            xEventGroupSetBits(g_network_event_group, g_network_connected_bit);
        } else if (event->ThreadConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Lost) {
            ESP_LOGW(TAG, "Thread connectivity lost");
            xEventGroupClearBits(g_network_event_group, g_network_connected_bit);
        }
        return;
    }

    if (event->Type == chip::DeviceLayer::DeviceEventType::kInternetConnectivityChange) {
        bool established = (event->InternetConnectivityChange.IPv4 == chip::DeviceLayer::kConnectivity_Established) ||
                            (event->InternetConnectivityChange.IPv6 == chip::DeviceLayer::kConnectivity_Established);
        bool lost = (event->InternetConnectivityChange.IPv4 == chip::DeviceLayer::kConnectivity_Lost) ||
                    (event->InternetConnectivityChange.IPv6 == chip::DeviceLayer::kConnectivity_Lost);

        if (established) {
            ESP_LOGI(TAG, "Internet connectivity established");
            xEventGroupSetBits(g_network_event_group, g_network_connected_bit);
        } else if (lost) {
            ESP_LOGW(TAG, "Internet connectivity lost");
            xEventGroupClearBits(g_network_event_group, g_network_connected_bit);
        }
    }
}

extern "C" void matter_set_network_event_group(EventGroupHandle_t event_group, EventBits_t connected_bit)
{
    g_network_event_group = event_group;
    g_network_connected_bit = connected_bit;
}

// Global state for Matter attributes
typedef struct {
    bool onoff;
    int16_t current_temp;
    int16_t aux_temp;
    uint16_t aux_humidity;
    int16_t heating_setpoint;
    int16_t cooling_setpoint;
    uint8_t system_mode;
    uint8_t compressor_demand;
    int16_t outdoor_temp;
    bool onoff_command_pending;
    uint8_t mode_command_pending;
    // Standalone "what HA actually wants," never touched by Tuya sync -- see
    // g_desired_setpoint_endpoint below. sync_task still re-reads this and
    // compares against Tuya's last poll every cycle regardless (self-healing
    // safety net if the immediate send below ever fails or gets missed), so
    // this value itself needs no latch/clear -- it always just reflects the
    // last thing HA asked for.
    int16_t desired_cooling_setpoint;
    // Added 2026-08-30: sync_task's periodic reconciliation alone meant a
    // real, successful setpoint change could sit unapplied to Tuya for up to
    // a full 5-minute poll interval after HA asked for it -- confirmed on
    // real hardware to cause visible confusion (checking the Tuya app
    // immediately after a change and seeing it not yet reflected). This flag
    // lets command_task notice the change and send it within its own 5-second
    // cycle instead, without touching the status-poll interval (the actual
    // quota-dominant call) at all -- see command_task in main.c.
    bool desired_setpoint_command_pending;
} matter_device_state_t;

static matter_device_state_t g_matter_state = {
    // Boot-time-only default, seeded before sync_task's first real Tuya poll
    // comes back a few seconds later (see the comment on that first-pass
    // fetch in main.c's sync_task). Seeding goes through update_attr_on_endpoint()
    // with g_internal_attr_update set, which matter_attribute_callback()
    // explicitly bypasses -- so this default was never capable of sending a
    // real command to Tuya, only of briefly *displaying* the wrong on/off
    // state in HA until the real poll lands. Defaulting to true rather than
    // false removes even that cosmetic flash for the common case (a reboot
    // of an already-running, already-on unit) at the cost of the flash
    // running the other way on the rare boot where the unit is genuinely
    // off -- accepted since a rare miscolored few-second UI flicker is a far
    // smaller cost than one that reads as the AC turning itself off.
    .onoff = true,
    .current_temp = 2200,
    .aux_temp = 2200,
    .aux_humidity = 5000,
    .heating_setpoint = 2000,
    .cooling_setpoint = 2400,
    .system_mode = 3,
    .compressor_demand = 0,
    .outdoor_temp = 0,
    .onoff_command_pending = false,
    .mode_command_pending = 0xFF,
    .desired_cooling_setpoint = 2400,
    .desired_setpoint_command_pending = false,
};

static node_t *g_node = nullptr;
static endpoint_t *g_endpoint = nullptr;
static endpoint_t *g_temp_sensor_endpoint = nullptr;
static endpoint_t *g_humidity_sensor_endpoint = nullptr;
static endpoint_t *g_outdoor_temp_sensor_endpoint = nullptr;
static endpoint_t *g_compressor_endpoint = nullptr;
static endpoint_t *g_compressor_running_endpoint = nullptr;
static endpoint_t *g_power_endpoint = nullptr;
static endpoint_t *g_desired_setpoint_endpoint = nullptr;
static uint16_t g_endpoint_id = 0;
static uint16_t g_temp_sensor_endpoint_id = 0;
static uint16_t g_humidity_sensor_endpoint_id = 0;
static uint16_t g_outdoor_temp_sensor_endpoint_id = 0;
static uint16_t g_compressor_endpoint_id = 0;
static uint16_t g_compressor_running_endpoint_id = 0;
static uint16_t g_power_endpoint_id = 0;
static uint16_t g_desired_setpoint_endpoint_id = 0;
static bool g_internal_attr_update = false;
static bool g_started = false;

// Persisted across reboots: onoff, system_mode, and desired_cooling_setpoint
// -- added 2026-08-30 after confirming on real hardware that
// desired_cooling_setpoint has no other resync path (everything else in
// matter_device_state_t is a mirror of Tuya or the BME280, refreshed from
// its real source within seconds to a few minutes of every boot regardless
// of what the hardcoded default is) and that onoff's existing "default to
// true" boot guess is only ever right for the common case, not the
// still-real one where the unit was genuinely off. Deliberately NOT
// persisting current_temp/heating_setpoint/cooling_setpoint/compressor_demand/
// outdoor_temp/aux_temp/aux_humidity or any of the *_command_pending flags --
// the mirrors gain nothing from it (already correct again within seconds)
// and the pending flags actively should NOT survive a reboot (there is no
// in-flight command from a session that no longer exists).
#define NVS_PERSIST_NAMESPACE "minisplit"
#define NVS_KEY_ONOFF "onoff"
#define NVS_KEY_SYSTEM_MODE "sys_mode"
#define NVS_KEY_DESIRED_SETPOINT "desired_sp"

static nvs_handle_t g_nvs_handle = 0;
static bool g_nvs_ready = false;

static void nvs_persist_init(void)
{
    esp_err_t err = nvs_open(NVS_PERSIST_NAMESPACE, NVS_READWRITE, &g_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace for persisted state (%s) -- "
                      "onoff/system_mode/desired_cooling_setpoint will fall back "
                      "to hardcoded defaults on every boot instead of surviving one",
                 esp_err_to_name(err));
        g_nvs_ready = false;
        return;
    }
    g_nvs_ready = true;
}

// Overrides the caller's hardcoded defaults in-place with whatever was last
// persisted, for whichever keys are actually present -- a fresh device (or
// one that just had its NVS erased) simply keeps the hardcoded defaults for
// every key that comes back ESP_ERR_NVS_NOT_FOUND, rather than failing.
static void nvs_load_persisted_state(matter_device_state_t *state)
{
    if (!g_nvs_ready) {
        return;
    }
    uint8_t onoff_u8 = 0;
    if (nvs_get_u8(g_nvs_handle, NVS_KEY_ONOFF, &onoff_u8) == ESP_OK) {
        state->onoff = (onoff_u8 != 0);
    }
    uint8_t system_mode = 0;
    if (nvs_get_u8(g_nvs_handle, NVS_KEY_SYSTEM_MODE, &system_mode) == ESP_OK) {
        state->system_mode = system_mode;
    }
    int16_t desired_setpoint = 0;
    if (nvs_get_i16(g_nvs_handle, NVS_KEY_DESIRED_SETPOINT, &desired_setpoint) == ESP_OK) {
        state->desired_cooling_setpoint = desired_setpoint;
    }
    ESP_LOGI(TAG, "Loaded persisted state: onoff=%d system_mode=%u desired_cooling_setpoint=%d",
             state->onoff, state->system_mode, state->desired_cooling_setpoint);
}

static void nvs_persist_u8(const char *key, uint8_t value)
{
    if (!g_nvs_ready) {
        return;
    }
    if (nvs_set_u8(g_nvs_handle, key, value) == ESP_OK) {
        nvs_commit(g_nvs_handle);
    }
}

static void nvs_persist_i16(const char *key, int16_t value)
{
    if (!g_nvs_ready) {
        return;
    }
    if (nvs_set_i16(g_nvs_handle, key, value) == ESP_OK) {
        nvs_commit(g_nvs_handle);
    }
}

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

    // g_endpoint (the Thermostat) still handles mode commands as before, and
    // mirrors Tuya's confirmed setpoint (read-only from HA's side now -- see
    // the rejection below). g_power_endpoint is the dedicated true-power
    // On/Off Plug-in Unit added alongside it -- see matter_device_init() --
    // distinct from the Thermostat's own Cool/Off cycling, which (per
    // main.c's command_task()) means "idle in fan mode", not a real
    // power-down. g_desired_setpoint_endpoint is the standalone entity HA
    // actually writes a target temperature to now -- see below.
    bool is_relevant_endpoint = (endpoint_id == g_endpoint_id || endpoint_id == g_power_endpoint_id ||
                                  endpoint_id == g_desired_setpoint_endpoint_id);
    if (!val || !is_relevant_endpoint || g_internal_attr_update) {
        return ESP_OK;
    }

    if (type != attribute::PRE_UPDATE) {
        return ESP_OK;
    }

    if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
        g_matter_state.onoff = val->val.b;
        g_matter_state.onoff_command_pending = true;
        ESP_LOGI(TAG, "OnOff command from Matter (endpoint %u, %s): %s", endpoint_id,
                 endpoint_id == g_power_endpoint_id ? "true power" : "thermostat",
                 g_matter_state.onoff ? "ON" : "OFF");
        return ESP_OK;
    }

    if (endpoint_id == g_desired_setpoint_endpoint_id && cluster_id == Thermostat::Id &&
        attribute_id == Thermostat::Attributes::OccupiedCoolingSetpoint::Id) {
        if (g_matter_state.desired_cooling_setpoint != val->val.i16) {
            g_matter_state.desired_cooling_setpoint = val->val.i16;
            nvs_persist_i16(NVS_KEY_DESIRED_SETPOINT, g_matter_state.desired_cooling_setpoint);
        }
        g_matter_state.desired_setpoint_command_pending = true;
        ESP_LOGI(TAG, "Desired setpoint set to %d (0.01C)", g_matter_state.desired_cooling_setpoint);
        return ESP_OK;
    }

    if (endpoint_id == g_endpoint_id && cluster_id == Thermostat::Id) {
        if (attribute_id == Thermostat::Attributes::OccupiedHeatingSetpoint::Id ||
            attribute_id == Thermostat::Attributes::OccupiedCoolingSetpoint::Id) {
            // This endpoint's setpoint is meant to be read-only now -- a
            // plain mirror of whatever Tuya last confirmed (see
            // apply_status_to_matter() in main.c). Real intent goes through
            // g_desired_setpoint_endpoint instead, which sync_task
            // reconciles against Tuya on every poll. Rejecting the write
            // here (rather than silently accepting it and letting the next
            // poll paper over it) is the actual "read-only" enforcement;
            // NOT CONFIRMED on real hardware whether this esp-matter/CHIP
            // version honors a non-OK PRE_UPDATE return as a write refusal
            // versus just logging it and committing anyway -- if it turns
            // out to be the latter, the value would still self-correct on
            // the next sync_task poll regardless, just not instantaneously.
            ESP_LOGW(TAG, "Rejecting write to read-only setpoint on thermostat endpoint "
                          "(attr=0x%08" PRIx32 ") -- use the Desired Setpoint entity instead",
                     attribute_id);
            return ESP_ERR_NOT_SUPPORTED;
        }
        if (attribute_id == Thermostat::Attributes::SystemMode::Id) {
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

    // Override g_matter_state's hardcoded onoff/system_mode/desired_cooling_setpoint
    // defaults with whatever was last persisted, before anything below builds
    // an endpoint config from them -- see nvs_load_persisted_state()'s
    // comment for why only these three fields.
    nvs_persist_init();
    nvs_load_persisted_state(&g_matter_state);

    // Without this, Basic Information's NodeLabel stays empty and Home
    // Assistant falls back to a generic "Node <id>" device name.
    node::config_t node_cfg;
    strncpy(node_cfg.root_node.basic_information.node_label, "Mini-Split AC Bridge",
            sizeof(node_cfg.root_node.basic_information.node_label) - 1);
    g_node = node::create(&node_cfg, matter_attribute_callback, nullptr);
    if (!g_node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return ESP_FAIL;
    }

    // Seed the cluster with real initial values at creation time rather than
    // relying on the update_attr() calls below -- those go through
    // update_attr_on_endpoint(), which no-ops until g_started is true, and
    // g_started isn't set until matter_start_commissioning() runs later.
    // Without this, a commissioner's very first read sees esp_matter's
    // built-in defaults: system_mode=Auto (1) despite this endpoint never
    // advertising the AutoMode feature (only Heat+Cool), which is a spec
    // violation a Matter controller can reasonably refuse to act on --
    // plausible root cause for setpoints showing as read-only in HA.
    endpoint::thermostat::config_t endpoint_cfg;
    endpoint_cfg.thermostat.feature_flags =
        cluster::thermostat::feature::heating::get_id() |
        cluster::thermostat::feature::cooling::get_id();
    endpoint_cfg.thermostat.system_mode = g_matter_state.system_mode;
    endpoint_cfg.thermostat.local_temperature = nullable<int16_t>(g_matter_state.current_temp);
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

    // Optional Thermostat attributes for compressor load and outdoor temperature.
    // Not part of the endpoint's default attribute set, so they're added manually.
    cluster_t *thermostat_cluster = cluster::get(g_endpoint, Thermostat::Id);
    if (thermostat_cluster) {
        cluster::thermostat::attribute::create_pi_cooling_demand(thermostat_cluster, 0);
        cluster::thermostat::attribute::create_pi_heating_demand(thermostat_cluster, 0);
        cluster::thermostat::attribute::create_outdoor_temperature(thermostat_cluster, nullable<int16_t>());
    } else {
        ESP_LOGW(TAG, "Thermostat cluster not found; compressor demand / outdoor temperature unavailable");
    }

    // Standalone temperature sensor endpoint. SmartThings exposes this as a
    // Temperature Measurement capability that can be used as a routine trigger.
    endpoint::temperature_sensor::config_t temp_sensor_cfg;
    temp_sensor_cfg.temperature_measurement.measured_value = nullable<int16_t>(g_matter_state.aux_temp);
    g_temp_sensor_endpoint = endpoint::temperature_sensor::create(g_node, &temp_sensor_cfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!g_temp_sensor_endpoint) {
        ESP_LOGE(TAG, "Failed to create Temperature Sensor endpoint");
        return ESP_FAIL;
    }
    g_temp_sensor_endpoint_id = endpoint::get_id(g_temp_sensor_endpoint);
    if (!g_temp_sensor_endpoint_id) {
        ESP_LOGE(TAG, "Failed to resolve Temperature Sensor endpoint id");
        return ESP_FAIL;
    }

    // Standalone humidity sensor endpoint. SmartThings exposes this as a
    // Relative Humidity Measurement capability usable as a routine trigger.
    endpoint::humidity_sensor::config_t humidity_sensor_cfg;
    humidity_sensor_cfg.relative_humidity_measurement.measured_value = nullable<uint16_t>(g_matter_state.aux_humidity);
    g_humidity_sensor_endpoint = endpoint::humidity_sensor::create(g_node, &humidity_sensor_cfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!g_humidity_sensor_endpoint) {
        ESP_LOGE(TAG, "Failed to create Humidity Sensor endpoint");
        return ESP_FAIL;
    }
    g_humidity_sensor_endpoint_id = endpoint::get_id(g_humidity_sensor_endpoint);
    if (!g_humidity_sensor_endpoint_id) {
        ESP_LOGE(TAG, "Failed to resolve Humidity Sensor endpoint id");
        return ESP_FAIL;
    }

    // Standalone outdoor temperature sensor endpoint. Same pattern as the aux
    // indoor temperature/humidity sensors above -- SmartThings only reliably
    // renders sensor data that lives on its own endpoint, not extra attributes
    // bundled onto the Thermostat cluster (confirmed: PICoolingDemand and the
    // Thermostat's own OutdoorTemperature attribute do not show up in the app).
    endpoint::temperature_sensor::config_t outdoor_temp_sensor_cfg;
    outdoor_temp_sensor_cfg.temperature_measurement.measured_value = nullable<int16_t>();
    g_outdoor_temp_sensor_endpoint = endpoint::temperature_sensor::create(g_node, &outdoor_temp_sensor_cfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!g_outdoor_temp_sensor_endpoint) {
        ESP_LOGE(TAG, "Failed to create Outdoor Temperature Sensor endpoint");
        return ESP_FAIL;
    }
    g_outdoor_temp_sensor_endpoint_id = endpoint::get_id(g_outdoor_temp_sensor_endpoint);
    if (!g_outdoor_temp_sensor_endpoint_id) {
        ESP_LOGE(TAG, "Failed to resolve Outdoor Temperature Sensor endpoint id");
        return ESP_FAIL;
    }

    // Compressor load endpoint. Matter has no dedicated cluster for
    // "percent load," so this repurposes a Humidity Sensor endpoint:
    // RelativeHumidityMeasurement's native %RH x100 scale is an exact,
    // unscaled match for a 0-100% reading, and -- unlike LevelControl -- it's
    // a plain read-only measurement, not something Home Assistant renders as
    // an actionable slider.
    endpoint::humidity_sensor::config_t compressor_demand_cfg;
    compressor_demand_cfg.relative_humidity_measurement.measured_value = nullable<uint16_t>((uint16_t)0);
    g_compressor_endpoint = endpoint::humidity_sensor::create(g_node, &compressor_demand_cfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!g_compressor_endpoint) {
        ESP_LOGE(TAG, "Failed to create Compressor Demand endpoint");
        return ESP_FAIL;
    }
    g_compressor_endpoint_id = endpoint::get_id(g_compressor_endpoint);
    if (!g_compressor_endpoint_id) {
        ESP_LOGE(TAG, "Failed to resolve Compressor Demand endpoint id");
        return ESP_FAIL;
    }

    // Compressor running state. Originally a Contact Sensor (BooleanState
    // cluster) endpoint, but confirmed on real hardware that BooleanState's
    // StateValue attribute in this esp-matter/connectedhomeip version has
    // been migrated to a newer C++ "codegen v2" cluster class
    // (BooleanStateCluster::SetStateValue()) whose live value bypasses
    // esp_matter's generic attribute store entirely -- attribute::update()
    // against it fails with ESP_ERR_NOT_SUPPORTED
    // (ATTRIBUTE_FLAG_MANAGED_INTERNALLY, see esp_matter_data_model.cpp's
    // set_val_internal()), and esp_matter exposes no public accessor to the
    // registered cluster instance to call SetStateValue() directly. Occupancy
    // Sensor's OccupancySensing.Occupancy attribute is still on the classic
    // attribute-store path (ATTRIBUTE_FLAG_NONE) in this SDK version, so it
    // actually works with attribute::update() -- used instead. Still renders
    // in Home Assistant as a read-only binary_sensor (Detected/Clear) with
    // full history, not something the user can (fruitlessly) toggle.
    endpoint::occupancy_sensor::config_t compressor_running_cfg;
    compressor_running_cfg.occupancy_sensing.occupancy = 0;
    compressor_running_cfg.occupancy_sensing.occupancy_sensor_type = 0;
    compressor_running_cfg.occupancy_sensing.feature_flags = cluster::occupancy_sensing::feature::other::get_id();
    g_compressor_running_endpoint = endpoint::occupancy_sensor::create(g_node, &compressor_running_cfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!g_compressor_running_endpoint) {
        ESP_LOGE(TAG, "Failed to create Compressor Running endpoint");
        return ESP_FAIL;
    }
    g_compressor_running_endpoint_id = endpoint::get_id(g_compressor_running_endpoint);
    if (!g_compressor_running_endpoint_id) {
        ESP_LOGE(TAG, "Failed to resolve Compressor Running endpoint id");
        return ESP_FAIL;
    }

    // Dedicated true-power endpoint (On/Off Plug-in Unit), separate from the
    // Thermostat's own Cool/Off cycling. The Thermostat's SystemMode "Off"
    // (see main.c's command_task()) now means "idle in fan mode, fresh-air
    // valve open" -- NOT a real power-down -- so this endpoint's OnOff
    // cluster is the only Matter/HA-reachable control that maps to
    // tuya_set_power() and genuinely powers the physical unit off/on.
    // Renders in Home Assistant as its own switch entity; rename it
    // something unambiguous (e.g. "MiniSplit Main Power") once commissioned,
    // to keep it clearly distinct from the thermostat's Cool/Off control.
    endpoint::on_off_plug_in_unit::config_t power_cfg;
    power_cfg.on_off.on_off = g_matter_state.onoff;
    g_power_endpoint = endpoint::on_off_plug_in_unit::create(g_node, &power_cfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!g_power_endpoint) {
        ESP_LOGE(TAG, "Failed to create Power endpoint");
        return ESP_FAIL;
    }
    g_power_endpoint_id = endpoint::get_id(g_power_endpoint);
    if (!g_power_endpoint_id) {
        ESP_LOGE(TAG, "Failed to resolve Power endpoint id");
        return ESP_FAIL;
    }

    // Standalone "Desired Setpoint" endpoint -- a second, minimal Thermostat
    // cluster (cooling-only; this is a cooling-only setup throughout) whose
    // only job is to hold whatever HA last asked for. Never written by
    // sync_task, so it carries none of the confirmation-lag/revert noise the
    // main thermostat endpoint's setpoint used to have; main.c's sync_task
    // just compares it against Tuya's own polled value every cycle and
    // resends when they disagree. SystemMode is seeded to Cool (not Off) so
    // HA's climate card doesn't grey out the setpoint control; nothing ever
    // reads or reacts to this endpoint's own mode/local-temperature, they
    // exist only because the Thermostat cluster requires them.
    endpoint::thermostat::config_t desired_setpoint_cfg;
    desired_setpoint_cfg.thermostat.feature_flags = cluster::thermostat::feature::cooling::get_id();
    desired_setpoint_cfg.thermostat.system_mode = 3; // kCool
    desired_setpoint_cfg.thermostat.local_temperature = nullable<int16_t>();
    desired_setpoint_cfg.thermostat.features.cooling.occupied_cooling_setpoint = g_matter_state.desired_cooling_setpoint;
    g_desired_setpoint_endpoint = endpoint::thermostat::create(g_node, &desired_setpoint_cfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!g_desired_setpoint_endpoint) {
        ESP_LOGE(TAG, "Failed to create Desired Setpoint endpoint");
        return ESP_FAIL;
    }
    g_desired_setpoint_endpoint_id = endpoint::get_id(g_desired_setpoint_endpoint);
    if (!g_desired_setpoint_endpoint_id) {
        ESP_LOGE(TAG, "Failed to resolve Desired Setpoint endpoint id");
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
    update_attr_on_endpoint(g_temp_sensor_endpoint, g_temp_sensor_endpoint_id,
                            TemperatureMeasurement::Id,
                            TemperatureMeasurement::Attributes::MeasuredValue::Id,
                            esp_matter_nullable_int16(nullable<int16_t>(g_matter_state.aux_temp)));
    update_attr_on_endpoint(g_humidity_sensor_endpoint, g_humidity_sensor_endpoint_id,
                            RelativeHumidityMeasurement::Id,
                            RelativeHumidityMeasurement::Attributes::MeasuredValue::Id,
                            esp_matter_nullable_uint16(nullable<uint16_t>(g_matter_state.aux_humidity)));
    update_attr_on_endpoint(g_outdoor_temp_sensor_endpoint, g_outdoor_temp_sensor_endpoint_id,
                            TemperatureMeasurement::Id,
                            TemperatureMeasurement::Attributes::MeasuredValue::Id,
                            esp_matter_nullable_int16(nullable<int16_t>()));
    update_attr_on_endpoint(g_compressor_endpoint, g_compressor_endpoint_id,
                            RelativeHumidityMeasurement::Id,
                            RelativeHumidityMeasurement::Attributes::MeasuredValue::Id,
                            esp_matter_nullable_uint16(nullable<uint16_t>((uint16_t)0)));
    update_attr_on_endpoint(g_compressor_running_endpoint, g_compressor_running_endpoint_id,
                            OccupancySensing::Id, OccupancySensing::Attributes::Occupancy::Id,
                            esp_matter_bitmap8(0));
    update_attr_on_endpoint(g_power_endpoint, g_power_endpoint_id,
                            OnOff::Id, OnOff::Attributes::OnOff::Id,
                            esp_matter_bool(g_matter_state.onoff));
    update_attr_on_endpoint(g_desired_setpoint_endpoint, g_desired_setpoint_endpoint_id,
                            Thermostat::Id, Thermostat::Attributes::OccupiedCoolingSetpoint::Id,
                            esp_matter_int16(g_matter_state.desired_cooling_setpoint));

    ESP_LOGI(TAG, "Matter device initialized: thermostat_ep=%u temp_sensor_ep=%u humidity_sensor_ep=%u outdoor_temp_sensor_ep=%u compressor_ep=%u compressor_running_ep=%u power_ep=%u desired_setpoint_ep=%u",
             g_endpoint_id, g_temp_sensor_endpoint_id,
             g_humidity_sensor_endpoint_id, g_outdoor_temp_sensor_endpoint_id, g_compressor_endpoint_id,
             g_compressor_running_endpoint_id, g_power_endpoint_id, g_desired_setpoint_endpoint_id);
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

    chip::DeviceLayer::PlatformMgr().AddEventHandler(app_chip_event_handler, 0);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    // esp_matter::start() initializes the Thread stack internally (ThreadStackMgr().InitThreadStack()),
    // but that call asserts on a platform config that must be supplied by the app beforehand -
    // esp_matter has no default of its own. ESP32-C6 has a native 802.15.4 radio, so no RCP/UART
    // link is needed; this device also has no separate CLI host, hence HOST_CONNECTION_MODE_NONE.
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

    // VendorName/ProductName: NOT set here. connectedhomeip's
    // BasicInformationCluster.cpp reads them directly from
    // CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME/_PRODUCT_NAME via
    // DeviceInstanceInfoProvider, bypassing esp_matter's attribute store
    // entirely -- an attribute::update() call here is a silent no-op
    // (confirmed on real hardware: HA still showed "TEST_PRODUCT" despite
    // this code running). See include/chip_project_config.h, wired in via
    // CONFIG_CHIP_PROJECT_CONFIG in sdkconfig.defaults, for the actual fix.

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
    nvs_persist_u8(NVS_KEY_ONOFF, on_off ? 1 : 0);
    update_attr(OnOff::Id, OnOff::Attributes::OnOff::Id, esp_matter_bool(on_off));
    update_attr_on_endpoint(g_power_endpoint, g_power_endpoint_id,
                            OnOff::Id, OnOff::Attributes::OnOff::Id, esp_matter_bool(on_off));
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
    if (g_matter_state.system_mode != mode) {
        g_matter_state.system_mode = mode;
        nvs_persist_u8(NVS_KEY_SYSTEM_MODE, mode);
    }
    update_attr(Thermostat::Id, Thermostat::Attributes::SystemMode::Id, esp_matter_enum8(mode));
}

extern "C" void matter_update_compressor_demand(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    g_matter_state.compressor_demand = percent;

    // Thermostat sub-attributes: correct per spec, but SmartThings doesn't render them.
    update_attr(Thermostat::Id, Thermostat::Attributes::PICoolingDemand::Id, esp_matter_uint8(percent));
    update_attr(Thermostat::Id, Thermostat::Attributes::PIHeatingDemand::Id, esp_matter_uint8(0));

    // Dedicated endpoint: a Humidity Sensor repurposed for its %RH x100 scale
    // (an exact match for 0-100%, no rescaling needed). Running state is a
    // separate endpoint -- see matter_update_compressor_running().
    update_attr_on_endpoint(g_compressor_endpoint, g_compressor_endpoint_id,
                            RelativeHumidityMeasurement::Id,
                            RelativeHumidityMeasurement::Attributes::MeasuredValue::Id,
                            esp_matter_nullable_uint16(nullable<uint16_t>((uint16_t)(percent * 100))));
}

extern "C" void matter_update_compressor_running(bool running)
{
    // OccupancySensing's Occupancy is a bitmap8; bit 0 = Occupied.
    update_attr_on_endpoint(g_compressor_running_endpoint, g_compressor_running_endpoint_id,
                            OccupancySensing::Id, OccupancySensing::Attributes::Occupancy::Id,
                            esp_matter_bitmap8(running ? 1 : 0));
}

extern "C" void matter_update_outdoor_temperature(int16_t temp_c)
{
    g_matter_state.outdoor_temp = temp_c;
    // Thermostat sub-attribute: correct per spec, but SmartThings doesn't render it.
    update_attr(Thermostat::Id, Thermostat::Attributes::OutdoorTemperature::Id,
                esp_matter_nullable_int16(nullable<int16_t>(temp_c)));
    // Dedicated sensor endpoint: the pattern SmartThings actually displays.
    update_attr_on_endpoint(g_outdoor_temp_sensor_endpoint, g_outdoor_temp_sensor_endpoint_id,
                            TemperatureMeasurement::Id,
                            TemperatureMeasurement::Attributes::MeasuredValue::Id,
                            esp_matter_nullable_int16(nullable<int16_t>(temp_c)));
}

extern "C" void matter_update_aux_temperature(int16_t temp_c)
{
    g_matter_state.aux_temp = temp_c;
    update_attr_on_endpoint(g_temp_sensor_endpoint, g_temp_sensor_endpoint_id,
                            TemperatureMeasurement::Id,
                            TemperatureMeasurement::Attributes::MeasuredValue::Id,
                            esp_matter_nullable_int16(nullable<int16_t>(temp_c)));
}

extern "C" void matter_update_aux_humidity(uint16_t humidity_centi_pct)
{
    g_matter_state.aux_humidity = humidity_centi_pct;
    update_attr_on_endpoint(g_humidity_sensor_endpoint, g_humidity_sensor_endpoint_id,
                            RelativeHumidityMeasurement::Id,
                            RelativeHumidityMeasurement::Attributes::MeasuredValue::Id,
                            esp_matter_nullable_uint16(nullable<uint16_t>(humidity_centi_pct)));
}

extern "C" bool matter_get_onoff_command(void)
{
    return g_matter_state.onoff_command_pending;
}

extern "C" bool matter_get_onoff_state(void)
{
    return g_matter_state.onoff;
}

extern "C" int16_t matter_get_desired_cooling_setpoint(void)
{
    return g_matter_state.desired_cooling_setpoint;
}

extern "C" bool matter_get_desired_setpoint_command_pending(void)
{
    return g_matter_state.desired_setpoint_command_pending;
}

extern "C" uint8_t matter_get_system_mode_command(void)
{
    return g_matter_state.mode_command_pending;
}

extern "C" void matter_clear_onoff_command(void)
{
    g_matter_state.onoff_command_pending = false;
}

extern "C" void matter_clear_mode_command(void)
{
    g_matter_state.mode_command_pending = 0xFF;
}

extern "C" void matter_clear_desired_setpoint_command_pending(void)
{
    g_matter_state.desired_setpoint_command_pending = false;
}

extern "C" void matter_device_deinit(void)
{
    g_node = nullptr;
    g_endpoint = nullptr;
    g_temp_sensor_endpoint = nullptr;
    g_humidity_sensor_endpoint = nullptr;
    g_outdoor_temp_sensor_endpoint = nullptr;
    g_compressor_endpoint = nullptr;
    g_compressor_running_endpoint = nullptr;
    g_power_endpoint = nullptr;
    g_desired_setpoint_endpoint = nullptr;
    g_endpoint_id = 0;
    g_temp_sensor_endpoint_id = 0;
    g_humidity_sensor_endpoint_id = 0;
    g_outdoor_temp_sensor_endpoint_id = 0;
    g_compressor_endpoint_id = 0;
    g_compressor_running_endpoint_id = 0;
    g_power_endpoint_id = 0;
    g_desired_setpoint_endpoint_id = 0;
    g_started = false;
    if (g_nvs_ready) {
        nvs_close(g_nvs_handle);
        g_nvs_ready = false;
    }
    ESP_LOGI(TAG, "Matter device deinitialized");
}
