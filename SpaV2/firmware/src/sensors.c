// Water temperature (DS18B20, 1-Wire over RMT) and pump current (SCT-013-030).

#include "sensors.h"
#include "spa_config.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "onewire_bus.h"
#include "ds18b20.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "sensors";

// ---------------- DS18B20 ----------------

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static float s_temp_c;
static bool s_temp_ok;

static void temp_task(void *arg)
{
    onewire_bus_handle_t bus;
    onewire_bus_config_t bcfg = {
        .bus_gpio_num = PIN_TEMP,
        .flags.en_pull_up = true,   // backstop only; the 4.7k external pull-up is what counts
    };
    onewire_bus_rmt_config_t rcfg = { .max_rx_bytes = 10 };
    if (onewire_new_bus_rmt(&bcfg, &rcfg, &bus) != ESP_OK) {
        ESP_LOGE(TAG, "1-Wire bus init failed on GPIO%d", PIN_TEMP);
        vTaskDelete(NULL);
    }

    ds18b20_device_handle_t ds = NULL;
    bool warned = false;
    for (;;) {
        if (!ds) {
            onewire_device_iter_handle_t iter;
            onewire_device_t dev;
            if (onewire_new_device_iter(bus, &iter) == ESP_OK) {
                while (onewire_device_iter_get_next(iter, &dev) == ESP_OK) {
                    ds18b20_config_t cfg = {};
                    if (ds18b20_new_device_from_enumeration(&dev, &cfg, &ds) == ESP_OK) {
                        ESP_LOGI(TAG, "DS18B20 found on GPIO%d, address %016llX", PIN_TEMP, dev.address);
                        warned = false;
                        break;
                    }
                }
                onewire_del_device_iter(iter);
            }
            if (!ds) {
                if (!warned) ESP_LOGW(TAG, "No DS18B20 on GPIO%d; retrying every 5 s", PIN_TEMP);
                warned = true;
                portENTER_CRITICAL(&s_mux);
                s_temp_ok = false;
                portEXIT_CRITICAL(&s_mux);
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }
        float t;
        bool ok = ds18b20_trigger_temperature_conversion(ds) == ESP_OK &&
                  ds18b20_get_temperature(ds, &t) == ESP_OK &&
                  t > -50.0f && t < 100.0f;             // 85.0 C is the DS18B20 power-on value
        if (ok && t == 85.0f) ok = false;
        portENTER_CRITICAL(&s_mux);
        s_temp_ok = ok;
        if (ok) s_temp_c = t;
        portEXIT_CRITICAL(&s_mux);
        if (!ok) {
            ESP_LOGW(TAG, "DS18B20 read failed; searching again");
            ds18b20_del_device(ds);
            ds = NULL;
        }
        vTaskDelay(pdMS_TO_TICKS(TEMP_PERIOD_MS));
    }
}

esp_err_t water_temp_init(void)
{
    return xTaskCreate(temp_task, "temp", 4096, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

bool water_temp_get(float *celsius)
{
    portENTER_CRITICAL(&s_mux);
    bool ok = s_temp_ok;
    *celsius = s_temp_c;
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

// ---------------- Pump CT ----------------

static pump_state_t s_pump;
static int64_t s_pump_on_since_us;

static void ct_task(void *arg)
{
    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_chan_cfg_t ccfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    adc_cali_handle_t cali;
    adc_cali_curve_fitting_config_t kcfg = {
        .unit_id = ADC_UNIT_1, .chan = CT_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_oneshot_new_unit(&ucfg, &adc) != ESP_OK ||
        adc_oneshot_config_channel(adc, CT_ADC_CHANNEL, &ccfg) != ESP_OK ||
        adc_cali_create_scheme_curve_fitting(&kcfg, &cali) != ESP_OK) {
        ESP_LOGE(TAG, "CT ADC init failed");
        vTaskDelete(NULL);
    }

    static int mv[CT_SAMPLES];
    bool was_running = false;
    for (;;) {
        int64_t next = esp_timer_get_time();
        for (int i = 0; i < CT_SAMPLES; i++) {
            int raw = 0;
            adc_oneshot_read(adc, CT_ADC_CHANNEL, &raw);
            adc_cali_raw_to_voltage(cali, raw, &mv[i]);
            next += CT_PERIOD_US;
            while (esp_timer_get_time() < next) { }
        }
        double sum = 0, sq = 0;
        for (int i = 0; i < CT_SAMPLES; i++) sum += mv[i];
        double mean = sum / CT_SAMPLES;
        for (int i = 0; i < CT_SAMPLES; i++) { double d = mv[i] - mean; sq += d * d; }
        float amps = (float)sqrt(sq / CT_SAMPLES) / 1000.0f * CT_A_PER_V;

        bool running = was_running;
        if (amps > CT_ON_A) running = true;
        else if (amps < CT_OFF_A) running = false;
        int64_t now = esp_timer_get_time();
        if (running && !was_running) s_pump_on_since_us = now;
        if (running != was_running) {
            ESP_LOGI(TAG, "Pump %s (%.2f A)", running ? "ON" : "off", amps);
        }
        was_running = running;

        portENTER_CRITICAL(&s_mux);
        s_pump.valid = mean > 1300 && mean < 2000;     // bias divider present
        s_pump.running = running && s_pump.valid;
        s_pump.amps = amps;
        s_pump.bias_mv = (float)mean;
        s_pump.running_s = s_pump.running ? (unsigned)((now - s_pump_on_since_us) / 1000000) : 0;
        portEXIT_CRITICAL(&s_mux);

        vTaskDelay(pdMS_TO_TICKS(800));
    }
}

esp_err_t pump_ct_init(void)
{
    return xTaskCreate(ct_task, "ct", 4096, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

pump_state_t pump_ct_get(void)
{
    portENTER_CRITICAL(&s_mux);
    pump_state_t p = s_pump;
    portEXIT_CRITICAL(&s_mux);
    return p;
}
