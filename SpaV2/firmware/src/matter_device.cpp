// SpaV2 Matter-over-Thread node.
//
// Endpoint choices follow MiniSplit (src/matter_device.cpp), which proved them
// in this Home Assistant:
//   - numbers with no Matter cluster -> repurposed Humidity Sensor
//     (RelativeHumidityMeasurement.MeasuredValue, uint16, HA divides by 100
//     and applies no unit conversion; never a Temperature Sensor, which HA
//     converts C->F);
//   - booleans -> Occupancy Sensor (BooleanState's StateValue is managed
//     internally in this SDK, so attribute::update() can't set it);
//   - momentary buttons -> On/Off Plug-in Unit with start_up_on_off = null.
//
// Endpoint order (do not reorder: HA entity ids are assigned by it):
//   1  Water temperature      Temperature Sensor   C x100
//   2  pH                     Humidity (repurposed) pH x100
//   3  ORP                    Humidity (repurposed) mV + 2000
//   4  pH/ORP readings live   Occupancy            1 = values are current
//   5  Pump running           Occupancy            CT clamp
//   6  Pump current           Humidity (repurposed) A x100
//   7  SmarterSpa state       Humidity (repurposed) spa_state_t x100
//   8  Electrode hours        Humidity (repurposed) hours (HA shows hours/100)
//   9  Calibration status     Humidity (repurposed) cal_status_t x100
//   10-14 Calibrate pH 7 / pH 4 / pH 10 / ORP / Clear   On/Off Plug-in Unit
// (endpoint 0 is the root node, so these are Matter endpoints 1-14.)

#include "matter_device.h"

#include <cstring>
#include <esp_log.h>

#include <esp_matter.h>
#include <esp_matter_endpoint.h>
#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include "esp_openthread_types.h"
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

using namespace esp_matter;
using namespace chip::app::Clusters;

static const char *TAG = "matter";

enum {
    EP_TEMP, EP_PH, EP_ORP, EP_LIVE, EP_PUMP, EP_PUMP_A, EP_SPA_STATE, EP_HOURS, EP_CAL_STATUS,
    EP_BTN0, EP_COUNT = EP_BTN0 + BTN_COUNT,
};
static uint16_t s_ep[EP_COUNT];
static bool s_started;
static volatile bool s_internal_update;
static cal_button_cb_t s_button_cb;

static void update(int ep, uint32_t cluster, uint32_t attr, esp_matter_attr_val_t val)
{
    if (!s_started || !s_ep[ep]) return;
    s_internal_update = true;
    esp_err_t err = attribute::update(s_ep[ep], cluster, attr, &val);
    s_internal_update = false;
    if (err != ESP_OK) ESP_LOGW(TAG, "update ep%u failed: %s", s_ep[ep], esp_err_to_name(err));
}

static void update_humidity(int ep, int32_t value)
{
    if (value < 0) value = 0;
    if (value > 65534) value = 65534;
    update(ep, RelativeHumidityMeasurement::Id, RelativeHumidityMeasurement::Attributes::MeasuredValue::Id,
           esp_matter_nullable_uint16(nullable<uint16_t>((uint16_t)value)));
}

static void update_occupancy(int ep, bool on)
{
    update(ep, OccupancySensing::Id, OccupancySensing::Attributes::Occupancy::Id, esp_matter_bitmap8(on ? 1 : 0));
}

static esp_err_t attribute_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                              uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv)
{
    if (type != attribute::PRE_UPDATE || !val || s_internal_update) return ESP_OK;
    if (cluster_id != OnOff::Id || attribute_id != OnOff::Attributes::OnOff::Id) return ESP_OK;
    for (int b = 0; b < BTN_COUNT; b++) {
        if (endpoint_id == s_ep[EP_BTN0 + b] && val->val.b && s_button_cb) {
            s_button_cb((cal_button_t)b);       // app resets the switch to off after acting
        }
    }
    return ESP_OK;
}

static uint16_t add_humidity(node_t *node)
{
    endpoint::humidity_sensor::config_t cfg;
    cfg.relative_humidity_measurement.measured_value = nullable<uint16_t>();
    endpoint_t *ep = endpoint::humidity_sensor::create(node, &cfg, ENDPOINT_FLAG_NONE, nullptr);
    return ep ? endpoint::get_id(ep) : 0;
}

static uint16_t add_occupancy(node_t *node)
{
    endpoint::occupancy_sensor::config_t cfg;
    cfg.occupancy_sensing.occupancy = 0;
    cfg.occupancy_sensing.occupancy_sensor_type = 0;
    cfg.occupancy_sensing.feature_flags = cluster::occupancy_sensing::feature::other::get_id();
    endpoint_t *ep = endpoint::occupancy_sensor::create(node, &cfg, ENDPOINT_FLAG_NONE, nullptr);
    return ep ? endpoint::get_id(ep) : 0;
}

static uint16_t add_button(node_t *node)
{
    endpoint::on_off_plug_in_unit::config_t cfg;
    cfg.on_off.on_off = false;
    cfg.on_off_lighting.start_up_on_off = nullable<uint8_t>();   // don't force a state at boot
    endpoint_t *ep = endpoint::on_off_plug_in_unit::create(node, &cfg, ENDPOINT_FLAG_NONE, nullptr);
    return ep ? endpoint::get_id(ep) : 0;
}

esp_err_t matter_device_start(cal_button_cb_t cb)
{
    s_button_cb = cb;

    node::config_t node_cfg;
    strncpy(node_cfg.root_node.basic_information.node_label, "Spa Monitor",
            sizeof(node_cfg.root_node.basic_information.node_label) - 1);
    node_t *node = node::create(&node_cfg, attribute_cb, nullptr);
    if (!node) return ESP_FAIL;

    endpoint::temperature_sensor::config_t tcfg;
    tcfg.temperature_measurement.measured_value = nullable<int16_t>();
    endpoint_t *tep = endpoint::temperature_sensor::create(node, &tcfg, ENDPOINT_FLAG_NONE, nullptr);
    s_ep[EP_TEMP] = tep ? endpoint::get_id(tep) : 0;
    s_ep[EP_PH] = add_humidity(node);
    s_ep[EP_ORP] = add_humidity(node);
    s_ep[EP_LIVE] = add_occupancy(node);
    s_ep[EP_PUMP] = add_occupancy(node);
    s_ep[EP_PUMP_A] = add_humidity(node);
    s_ep[EP_SPA_STATE] = add_humidity(node);
    s_ep[EP_HOURS] = add_humidity(node);
    s_ep[EP_CAL_STATUS] = add_humidity(node);
    for (int b = 0; b < BTN_COUNT; b++) s_ep[EP_BTN0 + b] = add_button(node);

    for (int i = 0; i < EP_COUNT; i++) {
        if (!s_ep[i]) {
            ESP_LOGE(TAG, "Failed to create endpoint index %d", i);
            return ESP_FAIL;
        }
    }

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    // ESP32-C6 native 802.15.4 radio, no host link (same as MiniSplit/ThreadRouter).
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
        ESP_LOGE(TAG, "esp_matter::start failed: %s", esp_err_to_name(err));
        return err;
    }
    s_started = true;

    chip::RendezvousInformationFlags flags(chip::RendezvousInformationFlag::kBLE);
    PrintOnboardingCodes(flags);
    ESP_LOGI(TAG, "Matter started: endpoints %u..%u. Commission from wyse "
             "(Wyse5070DebSetup/commission-thread-device.sh).", s_ep[0], s_ep[EP_COUNT - 1]);
    return ESP_OK;
}

void matter_publish_water_temp(bool valid, float celsius)
{
    nullable<int16_t> v = valid ? nullable<int16_t>((int16_t)(celsius * 100.0f)) : nullable<int16_t>();
    update(EP_TEMP, TemperatureMeasurement::Id, TemperatureMeasurement::Attributes::MeasuredValue::Id,
           esp_matter_nullable_int16(v));
}

void matter_publish_ph(float ph)              { update_humidity(EP_PH, (int32_t)(ph * 100.0f + 0.5f)); }
void matter_publish_orp(float orp_mv)         { update_humidity(EP_ORP, (int32_t)(orp_mv + 2000.0f + 0.5f)); }
void matter_publish_readings_live(bool live)  { update_occupancy(EP_LIVE, live); }
void matter_publish_spa_state(int code)       { update_humidity(EP_SPA_STATE, code * 100); }
void matter_publish_electrode_hours(uint32_t h) { update_humidity(EP_HOURS, (int32_t)h); }
void matter_publish_cal_status(int code)      { update_humidity(EP_CAL_STATUS, code * 100); }

void matter_publish_pump(bool running, float amps)
{
    update_occupancy(EP_PUMP, running);
    update_humidity(EP_PUMP_A, (int32_t)(amps * 100.0f + 0.5f));
}

void matter_reset_button(cal_button_t button)
{
    update(EP_BTN0 + (int)button, OnOff::Id, OnOff::Attributes::OnOff::Id, esp_matter_bool(false));
}
