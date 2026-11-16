// Atlas Gravity pH / ORP through the Atlas analog isolators.
//
// Port of Atlas Scientific's atlas_gravity library (MIT, 2020): ph_grav,
// orp_grav and their isolated variants ph_iso_grav / orp_iso_grav. The
// isolated read_voltage() times the isolator's PWM high pulse with
// pulseIn(pin, HIGH, 400) and returns mV = average high time (us) * 35; with
// no pulses the pin level decides (80 us if high, else 0). Here the pulse
// timing is done by MCPWM capture (12.5 ns edge timestamps) and averaged over
// one second (~12k pulses vs the library's 3000). The pH/ORP conversion and
// calibration formulas are the library's, unchanged; Arduino EEPROM becomes
// NVS, and calibration gains stability/plausibility guards (PLAN.md Phase 3).

#include "probes.h"
#include "spa_config.h"

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/mcpwm_cap.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "probes";

// ---------------- PWM capture ----------------

typedef struct {
    gpio_num_t gpio;
    uint32_t last_rise;
    bool have_rise;
    uint64_t sum_high;           // capture ticks, taken by the sampler each second
    uint32_t n_high;
} cap_chan_t;

static cap_chan_t s_cap[2] = { { .gpio = PIN_PH }, { .gpio = PIN_ORP } };
static portMUX_TYPE s_cap_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_res_hz;

static bool IRAM_ATTR on_cap(mcpwm_cap_channel_handle_t cap, const mcpwm_capture_event_data_t *e, void *ctx)
{
    cap_chan_t *c = (cap_chan_t *)ctx;
    portENTER_CRITICAL_ISR(&s_cap_mux);
    if (e->cap_edge == MCPWM_CAP_EDGE_POS) {
        c->last_rise = e->cap_value;
        c->have_rise = true;
    } else if (c->have_rise) {
        c->sum_high += (uint32_t)(e->cap_value - c->last_rise);
        c->n_high++;
    }
    portEXIT_CRITICAL_ISR(&s_cap_mux);
    return false;
}

// atlas_gravity *_iso_grav::read_voltage(), with the 1 s average.
static float take_mv(cap_chan_t *c, bool *had_pulses)
{
    portENTER_CRITICAL(&s_cap_mux);
    uint64_t sum = c->sum_high;
    uint32_t n = c->n_high;
    c->sum_high = 0;
    c->n_high = 0;
    portEXIT_CRITICAL(&s_cap_mux);

    float high_us;
    if (n == 0) {
        high_us = gpio_get_level(c->gpio) ? 80.0f : 0.0f;
        *had_pulses = false;
    } else {
        high_us = (float)sum / n * 1e6f / s_res_hz;
        *had_pulses = true;
    }
    return high_us * 35.0f;
}

// ---------------- calibration (library math) ----------------

#define NVS_NS "probecal"

typedef struct {
    float ph_mid, ph_low, ph_high;   // mV at pH 7 / 4 / 10
    float orp_offset;
} cal_t;

static const cal_t CAL_DEFAULTS = { 1500.0f, 2030.0f, 975.0f, 0.0f };  // library cal_clear()
static cal_t s_cal;
static SemaphoreHandle_t s_lock;

static float lib_ph(float mv)                 // ph_grav.cpp read_ph()
{
    if (mv > s_cal.ph_mid) {
        return 7.0f - 3.0f / (s_cal.ph_low - s_cal.ph_mid) * (mv - s_cal.ph_mid);
    }
    return 7.0f - 3.0f / (s_cal.ph_mid - s_cal.ph_high) * (mv - s_cal.ph_mid);
}

static float lib_orp(float mv)                // orp_grav.cpp read_orp()
{
    return mv - 1500.0f - s_cal.orp_offset;
}

static void cal_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed; calibration not saved");
        return;
    }
    nvs_set_blob(h, "cal", &s_cal, sizeof(s_cal));
    nvs_commit(h);
    nvs_close(h);
}

static void cal_load(void)
{
    s_cal = CAL_DEFAULTS;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(s_cal);
    cal_t c;
    if (nvs_get_blob(h, "cal", &c, &len) == ESP_OK && len == sizeof(c)) s_cal = c;
    nvs_close(h);
}

// ---------------- sampling ----------------

typedef struct {
    float mv[PROBE_WINDOW];
    int count;                   // samples stored, saturates at PROBE_WINDOW
    int head;
    bool pulses;                 // last sample had pulses
} hist_t;

static hist_t s_hist[2];

static void hist_push(hist_t *h, float mv, bool pulses)
{
    h->mv[h->head] = mv;
    h->head = (h->head + 1) % PROBE_WINDOW;
    if (h->count < PROBE_WINDOW) h->count++;
    h->pulses = pulses;
}

// Mean of n samples, starting `skip` samples back from the newest.
static float hist_mean_at(const hist_t *h, int skip, int n)
{
    if (skip + n > h->count) n = h->count - skip;
    if (n <= 0) return 0;
    float sum = 0;
    for (int i = skip; i < skip + n; i++)
        sum += h->mv[(h->head - 1 - i + 2 * PROBE_WINDOW) % PROBE_WINDOW];
    return sum / n;
}

// Mean of the newest n samples.
static float hist_mean(const hist_t *h, int n) { return hist_mean_at(h, 0, n); }

static void sampler_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(PROBE_SAMPLE_MS));
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (int i = 0; i < 2; i++) {
            bool pulses;
            float mv = take_mv(&s_cap[i], &pulses);
            hist_push(&s_hist[i], mv, pulses);
        }
        xSemaphoreGive(s_lock);
    }
}

probe_reading_t probes_read(void)
{
    probe_reading_t r = {0};
    xSemaphoreTake(s_lock, portMAX_DELAY);
    r.ph_raw_mv = hist_mean(&s_hist[0], 10);
    r.orp_raw_mv = hist_mean(&s_hist[1], 10);
    r.valid = s_hist[0].pulses && s_hist[1].pulses && s_hist[0].count > 0;
    r.ph = lib_ph(r.ph_raw_mv);
    r.orp_mv = lib_orp(r.orp_raw_mv);
    xSemaphoreGive(s_lock);
    return r;
}

// ---------------- guarded calibration ----------------

// No stability or plausibility limits (owner's decision 2026-11-15: the spa is
// electrically noisy and a rejected press is worse than an imperfect one).
// A press stores the 30 s mean as-is; the log prints the drift and slopes so a
// bad point is visible. Only "no signal from the isolator" is refused.

cal_status_t probes_calibrate(cal_cmd_t cmd, float ref_mv)
{
    cal_status_t st = CAL_STATUS_OK;
    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (cmd == CAL_CMD_CLEAR || cmd == CAL_CMD_PH_CLEAR || cmd == CAL_CMD_ORP_CLEAR) {
        if (cmd != CAL_CMD_ORP_CLEAR) {             // ph_grav.cpp cal_clear()
            s_cal.ph_mid = CAL_DEFAULTS.ph_mid;
            s_cal.ph_low = CAL_DEFAULTS.ph_low;
            s_cal.ph_high = CAL_DEFAULTS.ph_high;
        }
        if (cmd != CAL_CMD_PH_CLEAR) s_cal.orp_offset = 0;   // orp_grav.cpp cal_clear()
        cal_save();
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "Calibration cleared (%s) to library defaults",
                 cmd == CAL_CMD_CLEAR ? "pH + ORP" : cmd == CAL_CMD_PH_CLEAR ? "pH" : "ORP");
        probes_log_calibration();
        return CAL_STATUS_CLEARED;
    }

    int idx = (cmd == CAL_CMD_ORP) ? 1 : 0;
    const hist_t *h = &s_hist[idx];
    float mv = hist_mean(h, PROBE_WINDOW);
    float drift = hist_mean_at(h, 0, 10) - hist_mean_at(h, PROBE_WINDOW - 10, 10);

    if (!h->pulses || h->count == 0) {
        st = CAL_STATUS_NO_SIGNAL;
    } else {
        switch (cmd) {
        case CAL_CMD_PH7:
            s_cal.ph_mid = mv;
            break;
        case CAL_CMD_PH4:
            s_cal.ph_low = mv;
            break;
        case CAL_CMD_PH10:
            s_cal.ph_high = mv;
            break;
        case CAL_CMD_ORP: {
            float ref = ref_mv > 0 ? ref_mv : ORP_CAL_REF_MV;
            s_cal.orp_offset = mv - (ref + 1500.0f);    // orp_grav.cpp cal()
            break;
        }
        default:
            break;
        }
        if (st == CAL_STATUS_OK) cal_save();
    }
    xSemaphoreGive(s_lock);

    static const char *names[] = { "pH 7", "pH 4", "pH 10", "ORP", "clear", "pH clear", "ORP clear" };
    ESP_LOGI(TAG, "Calibrate %s: %s (30 s mean %.1f mV, drift %+.1f mV over 20 s)",
             names[cmd], cal_status_name(st), mv, drift);
    probes_log_calibration();
    return st;
}

const char *cal_status_name(cal_status_t s)
{
    switch (s) {
    case CAL_STATUS_NONE: return "none";
    case CAL_STATUS_OK: return "OK";
    case CAL_STATUS_UNSTABLE: return "unstable";
    case CAL_STATUS_OUT_OF_RANGE: return "out of range";
    case CAL_STATUS_CLEARED: return "cleared";
    case CAL_STATUS_NO_SIGNAL: return "no signal";
    case CAL_STATUS_BUSY: return "calibrating";
    }
    return "?";
}

void probes_log_calibration(void)
{
    ESP_LOGI(TAG, "Cal: pH7=%.1f mV pH4=%.1f mV pH10=%.1f mV (acid slope %.0f, base slope %.0f mV/pH; "
             "nominal ~175) ORP offset=%.1f mV",
             s_cal.ph_mid, s_cal.ph_low, s_cal.ph_high,
             (s_cal.ph_low - s_cal.ph_mid) / 3.0f, (s_cal.ph_mid - s_cal.ph_high) / 3.0f,
             s_cal.orp_offset);
}

// ---------------- init ----------------

esp_err_t probes_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    cal_load();
    probes_log_calibration();

    mcpwm_cap_timer_handle_t timer;
    mcpwm_capture_timer_config_t tcfg = {
        .group_id = 0,
        .clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT,
    };
    esp_err_t err = mcpwm_new_capture_timer(&tcfg, &timer);
    if (err != ESP_OK) return err;
    mcpwm_capture_timer_get_resolution(timer, &s_res_hz);

    for (int i = 0; i < 2; i++) {
        mcpwm_cap_channel_handle_t ch;
        mcpwm_capture_channel_config_t ccfg = {
            .gpio_num = s_cap[i].gpio,
            .prescale = 1,
            .flags.pos_edge = true,
            .flags.neg_edge = true,
        };
        if ((err = mcpwm_new_capture_channel(timer, &ccfg, &ch)) != ESP_OK) return err;
        mcpwm_capture_event_callbacks_t cbs = { .on_cap = on_cap };
        if ((err = mcpwm_capture_channel_register_event_callbacks(ch, &cbs, &s_cap[i])) != ESP_OK) return err;
        if ((err = mcpwm_capture_channel_enable(ch)) != ESP_OK) return err;
    }
    if ((err = mcpwm_capture_timer_enable(timer)) != ESP_OK) return err;
    if ((err = mcpwm_capture_timer_start(timer)) != ESP_OK) return err;

    xTaskCreate(sampler_task, "probes", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "pH on GPIO%d, ORP on GPIO%d (isolator PWM, %lu Hz capture)", PIN_PH, PIN_ORP,
             (unsigned long)s_res_hz);
    return ESP_OK;
}
