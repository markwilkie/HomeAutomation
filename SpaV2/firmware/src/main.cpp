// SpaV2 spa monitor: Matter-over-Thread node on the ESP32-C6 DevKitM-1.
//
// Water temperature (DS18B20), pH and ORP (Atlas Gravity via isolators), pump
// running (SCT-013-030 CT), and the SmarterSpa salt-chlorine generator's state
// decoded from its LEDs. See ../../PLAN.md.

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"

#include "spa_config.h"
#include "probes.h"
#include "sensors.h"
#include "spa_leds.h"
#include "matter_device.h"

static const char *TAG = "spav2";

// ---------------- calibration requests (HA buttons + serial) ----------------

typedef struct {
    cal_cmd_t cmd;
    float ref_mv;           // ORP reference for CAL_CMD_ORP (<= 0: default)
    int button;             // cal_button_t to reset afterwards, or -1 (serial)
} cal_req_t;

static QueueHandle_t s_cal_q;
static int64_t s_cal_live_until_us;

static void on_cal_button(cal_button_t b)         // Matter task: queue and return
{
    static const cal_cmd_t map[BTN_COUNT] = {
        CAL_CMD_PH7, CAL_CMD_PH4, CAL_CMD_PH10, CAL_CMD_ORP, CAL_CMD_CLEAR,
    };
    cal_req_t r = { map[b], 0, (int)b };
    xQueueSend(s_cal_q, &r, 0);
}

static void handle_cal(const cal_req_t &r)
{
    // A calibration press means the probe is in a cup: publish live readings
    // for a while regardless of the pump gate.
    s_cal_live_until_us = esp_timer_get_time() + (int64_t)CAL_LIVE_S * 1000000;
    matter_publish_cal_status(CAL_STATUS_BUSY);
    vTaskDelay(pdMS_TO_TICKS(1000));
    cal_status_t st = probes_calibrate(r.cmd, r.ref_mv);
    matter_publish_cal_status(st);
    if (r.button >= 0) {
        vTaskDelay(pdMS_TO_TICKS(500));               // let HA see the press before it springs back
        matter_reset_button((cal_button_t)r.button);
    }
}

// ---------------- serial console (original sketch's commands) ----------------

static void console_task(void *arg)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "Serial console unavailable");
        vTaskDelete(NULL);
    }
    usb_serial_jtag_vfs_use_driver();
    setvbuf(stdin, NULL, _IONBF, 0);

    printf("Serial commands: PHCAL,7 | PHCAL,4 | PHCAL,10 | PHCAL,CLEAR | ORPCAL,<mV> | ORPCAL,CLEAR | STATUS\n");
    char line[64];
    size_t n = 0;
    for (;;) {
        int c = fgetc(stdin);
        if (c == EOF) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (c != '\n' && c != '\r') {
            if (n < sizeof(line) - 1) line[n++] = (char)c;
            continue;
        }
        line[n] = 0;
        n = 0;
        for (char *p = line; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
        if (!line[0]) continue;

        cal_req_t r = { CAL_CMD_CLEAR, 0, -1 };
        if (!strcmp(line, "PHCAL,7")) r.cmd = CAL_CMD_PH7;
        else if (!strcmp(line, "PHCAL,4")) r.cmd = CAL_CMD_PH4;
        else if (!strcmp(line, "PHCAL,10")) r.cmd = CAL_CMD_PH10;
        else if (!strcmp(line, "PHCAL,CLEAR")) r.cmd = CAL_CMD_PH_CLEAR;
        else if (!strcmp(line, "ORPCAL,CLEAR")) r.cmd = CAL_CMD_ORP_CLEAR;
        else if (!strncmp(line, "ORPCAL,", 7)) { r.cmd = CAL_CMD_ORP; r.ref_mv = strtof(line + 7, NULL); }
        else if (!strcmp(line, "STATUS")) { probes_log_calibration(); continue; }
        else { printf("Unknown command: %s\n", line); continue; }
        xQueueSend(s_cal_q, &r, 0);
    }
}

// ---------------- publisher ----------------

static void app_task(void *arg)
{
    float last_temp = NAN;
    bool last_temp_ok = false, last_live = false, last_pump = false, first = true;
    int last_spa = -1;
    uint32_t last_hours = UINT32_MAX, last_readout_seq = 0;
    int64_t last_probe_pub = 0, last_temp_pub = 0, last_amps_pub = 0, last_log = 0;

    for (;;) {
        cal_req_t r;
        if (xQueueReceive(s_cal_q, &r, pdMS_TO_TICKS(1000)) == pdTRUE) handle_cal(r);

        int64_t now = esp_timer_get_time();

        // Pump (CT)
        pump_state_t pump = pump_ct_get();
        if (first || pump.running != last_pump || now - last_amps_pub > 10000000LL) {
            matter_publish_pump(pump.running, pump.valid ? pump.amps : 0);
            last_pump = pump.running;
            last_amps_pub = now;
        }

        // pH / ORP, gated on the pump having run long enough (or a recent cal press).
        bool live = (pump.running && pump.running_s >= PUMP_SETTLE_S) || now < s_cal_live_until_us;
        probe_reading_t pr = probes_read();
        live = live && pr.valid;
        if (first || live != last_live) {
            matter_publish_readings_live(live);
            last_live = live;
        }
        if (live && now - last_probe_pub >= PROBE_PUBLISH_MS * 1000LL) {
            matter_publish_ph(pr.ph);
            matter_publish_orp(pr.orp_mv);
            last_probe_pub = now;
        }

        // Water temperature: every 30 s, or on a 0.1 C change.
        float t;
        bool tok = water_temp_get(&t);
        if (first || tok != last_temp_ok || (tok && fabsf(t - last_temp) >= 0.1f) ||
            now - last_temp_pub > 30000000LL) {
            matter_publish_water_temp(tok, t);
            last_temp = t;
            last_temp_ok = tok;
            last_temp_pub = now;
        }

        // SmarterSpa
        int spa = spa_leds_state();
        if (spa != last_spa) { matter_publish_spa_state(spa); last_spa = spa; }
        uint32_t hours = spa_leds_generating_hours();
        if (hours != last_hours) { matter_publish_electrode_hours(hours); last_hours = hours; }
        uint32_t rseq;
        int rval;
        if (spa_leds_last_readout(&rseq, &rval) && rseq != last_readout_seq) {
            matter_publish_spa_readout(rval);
            last_readout_seq = rseq;
        }

        if (first) matter_publish_cal_status(CAL_STATUS_NONE);
        first = false;

        if (now - last_log > 30000000LL) {
            last_log = now;
            ESP_LOGI(TAG, "pH %.2f (%.0f mV) ORP %+.0f mV (%.0f mV raw) %s | temp %s%.2f C | "
                     "pump %s %.2f A (bias %.0f mV, on %us) | spa %s, %lu electrode h",
                     pr.ph, pr.ph_raw_mv, pr.orp_mv, pr.orp_raw_mv,
                     !pr.valid ? "[NO PWM]" : live ? "[live]" : "[held: pump gate]",
                     tok ? "" : "[none] ", tok ? t : 0.0f,
                     pump.running ? "ON" : "off", pump.amps, pump.bias_mv, pump.running_s,
                     spa_state_name((spa_state_t)spa), (unsigned long)hours);
        }
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== SpaV2 Spa Monitor %s ===", esp_app_get_description()->version);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_cal_q = xQueueCreate(8, sizeof(cal_req_t));

    // Sensors first: a missing sensor logs and keeps retrying; it never blocks Matter.
    if (probes_init() != ESP_OK) ESP_LOGE(TAG, "pH/ORP capture init failed");
    if (water_temp_init() != ESP_OK) ESP_LOGE(TAG, "Water temperature init failed");
    if (pump_ct_init() != ESP_OK) ESP_LOGE(TAG, "Pump CT init failed");
    if (spa_leds_init() != ESP_OK) ESP_LOGE(TAG, "SmarterSpa LED decoder init failed");

    if (matter_device_start(on_cal_button) != ESP_OK) {
        ESP_LOGE(TAG, "Matter failed to start; sensors still run and log to serial");
    }

    xTaskCreate(app_task, "app", 6144, NULL, 5, NULL);
    xTaskCreate(console_task, "console", 3072, NULL, 2, NULL);
}
