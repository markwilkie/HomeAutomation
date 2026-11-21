// Atlas Gravity pH / ORP through the Atlas analog isolators.
//
// Port of Atlas Scientific's atlas_gravity library (MIT, 2020): ph_grav,
// orp_grav and their isolated variants ph_iso_grav / orp_iso_grav. The
// isolated read_voltage() times the isolator's PWM high pulse with
// pulseIn(pin, HIGH, 400) and returns mV = average high time (us) * 35; with
// no pulses the pin level decides (80 us if high, else 0). Here the pulse
// timing is done by MCPWM capture (12.5 ns edge timestamps), each pin averaged
// over half a second (thousands of pulses vs the library's 3000). The pH/ORP conversion and
// calibration formulas are the library's, unchanged; Arduino EEPROM becomes
// NVS, and calibration gains stability/plausibility guards (PLAN.md Phase 3).

#include "probes.h"
#include "spa_config.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/mcpwm_cap.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

static const char *TAG = "probes";

// ---------------- PWM capture ----------------
//
// Edge labels are not trusted (2026-11-20). With one channel capturing both
// edges, the driver reads the edge type from a status register separately from
// the latched time, and the pairing could lock onto the wrong phase: the pH
// input read 1403 mV in pH 7 buffer, then ~2190-2400 mV in the same buffer after
// a reboot (1403 + 2193 = one ~103 us period, i.e. the low pulse one boot and
// the high pulse the next), and pH 4 read below pH 7. Now each pin gets two
// channels, one capturing only rising edges and one only falling edges, so the
// edge type is known from which channel fired. The C6 has three capture
// channels, so the two pins take turns: each is measured for half of every
// PROBE_SAMPLE_MS.
//
// Glitches (2026-11-20, raw edge dump, serial EDGES,0): the isolator signal is a
// clean 80 us PWM, but its rising edges often carry a 10-100 ns glitch, captured
// as a fall plus a rise almost on top of each other (every rising edge on pH,
// some on ORP). Paired naively, those made high pulses of ~0 or ~80 us, which is
// what produced today's jumping and "inverted" readings. So edges closer than
// CAP_GLITCH_NS are merged into one transition, timed at its first edge, whose
// direction is that of its last edge. A pulse is then used only from a clean
// rise -> fall -> rise sequence with a 60-100 us period. High = fall - rise, as
// the library times it, but the value is read from the low time (see take_mv). Accepted and rejected counts are logged once a minute.

typedef struct {
    gpio_num_t gpio;
    uint64_t sum_high, sum_low, sum_period;   // capture ticks of accepted pulses
    uint32_t n_ok, n_rej;
} cap_chan_t;

static cap_chan_t s_cap[2] = { { .gpio = PIN_PH }, { .gpio = PIN_ORP } };
static portMUX_TYPE s_cap_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_res_hz;
static mcpwm_cap_timer_handle_t s_timer;
static mcpwm_cap_channel_handle_t s_ch_rise, s_ch_fall;

// Edge state for the pin being measured (reset at every switch; under s_cap_mux).
static cap_chan_t *s_cur;
static bool s_have_cl;                       // a transition (edge cluster) is open
static uint32_t s_cl_first, s_cl_last;       // its earliest and latest edge times
static bool s_cl_rise;                       // direction of its latest edge
static int s_seq;                            // 0 none, 1 have rise, 2 have rise + fall
static uint32_t s_rise_t, s_fall_t;
static uint32_t s_min_period_ticks, s_max_period_ticks, s_glitch_ticks;

#define CAP_MIN_PERIOD_US 60
#define CAP_GLITCH_NS     1000               // observed glitches are 10-100 ns; pulses are 30-50 us

// Raw edge dump (serial EDGES,0 / EDGES,1): the next DUMP_N edges on that pin.
#define DUMP_N 256
static volatile int s_dump_req = -1;     // probe index requested, -1 none
static bool s_dumping;
static int s_dump_n;
static uint32_t s_dump_t[DUMP_N];
static uint8_t s_dump_rise[DUMP_N];

static inline void IRAM_ATTR dump_edge(uint32_t v, bool rise)
{
    if (s_dumping && s_dump_n < DUMP_N) {
        s_dump_t[s_dump_n] = v;
        s_dump_rise[s_dump_n] = rise;
        s_dump_n++;
    }
}
#define CAP_MAX_PERIOD_US 100

// A finished transition at time t: rise -> fall -> rise completes a pulse.
static inline void IRAM_ATTR transition(uint32_t t, bool rise)
{
    if (rise) {
        if (s_seq == 2) {
            uint32_t period = t - s_rise_t;
            if (period >= s_min_period_ticks && period <= s_max_period_ticks) {
                s_cur->sum_high += s_fall_t - s_rise_t;
                s_cur->sum_low += t - s_fall_t;
                s_cur->sum_period += period;
                s_cur->n_ok++;
            } else {
                s_cur->n_rej++;
            }
        } else if (s_seq == 1) {
            s_cur->n_rej++;                      // rise after rise: a fall went missing
        }
        s_rise_t = t;
        s_seq = 1;
    } else if (s_seq == 1) {
        s_fall_t = t;
        s_seq = 2;
    } else {
        if (s_seq == 2) s_cur->n_rej++;          // fall after fall: a rise went missing
        s_seq = 0;
    }
}

static inline void IRAM_ATTR on_edge(uint32_t v, bool rise)
{
    portENTER_CRITICAL_ISR(&s_cap_mux);
    dump_edge(v, rise);
    if (s_cur) {
        int32_t d = (int32_t)(v - s_cl_first);
        if (s_have_cl && d > -(int32_t)s_glitch_ticks && d < (int32_t)s_glitch_ticks) {
            // Part of the open transition (the two channels' interrupts can come in either order).
            if ((int32_t)(v - s_cl_last) > 0) { s_cl_last = v; s_cl_rise = rise; }
            if (d < 0) s_cl_first = v;
        } else if (s_have_cl && d < 0) {
            s_cur->n_rej++;                      // an interrupt so late it is out of order: start over
            s_have_cl = false;
            s_seq = 0;
        } else {
            if (s_have_cl) transition(s_cl_first, s_cl_rise);
            s_have_cl = true;
            s_cl_first = s_cl_last = v;
            s_cl_rise = rise;
        }
    }
    portEXIT_CRITICAL_ISR(&s_cap_mux);
}

static bool IRAM_ATTR on_rise(mcpwm_cap_channel_handle_t cap, const mcpwm_capture_event_data_t *e, void *ctx)
{
    on_edge(e->cap_value, true);
    return false;
}

static bool IRAM_ATTR on_fall(mcpwm_cap_channel_handle_t cap, const mcpwm_capture_event_data_t *e, void *ctx)
{
    on_edge(e->cap_value, false);
    return false;
}

static esp_err_t cap_channel(gpio_num_t gpio, bool rise, mcpwm_cap_channel_handle_t *out)
{
    mcpwm_capture_channel_config_t ccfg = {
        .gpio_num = gpio,
        .prescale = 1,
        .flags.pos_edge = rise,
        .flags.neg_edge = !rise,
    };
    esp_err_t err = mcpwm_new_capture_channel(s_timer, &ccfg, out);
    if (err != ESP_OK) return err;
    mcpwm_capture_event_callbacks_t cbs = { .on_cap = rise ? on_rise : on_fall };
    if ((err = mcpwm_capture_channel_register_event_callbacks(*out, &cbs, NULL)) != ESP_OK) return err;
    return mcpwm_capture_channel_enable(*out);
}

static void cap_drop(mcpwm_cap_channel_handle_t *ch)
{
    if (!*ch) return;
    mcpwm_capture_channel_disable(*ch);
    mcpwm_del_capture_channel(*ch);
    *ch = NULL;
}

// Point both capture channels at probe i and start its slot.
static esp_err_t cap_measure(int i)
{
    portENTER_CRITICAL(&s_cap_mux);
    s_cur = NULL;
    portEXIT_CRITICAL(&s_cap_mux);
    cap_drop(&s_ch_rise);
    cap_drop(&s_ch_fall);
    portENTER_CRITICAL(&s_cap_mux);
    s_have_cl = false;
    s_seq = 0;
    s_cap[i].sum_high = s_cap[i].sum_low = s_cap[i].sum_period = 0;
    s_cap[i].n_ok = s_cap[i].n_rej = 0;
    s_cur = &s_cap[i];
    s_dump_n = 0;
    s_dumping = (s_dump_req == i);
    portEXIT_CRITICAL(&s_cap_mux);
    esp_err_t err = cap_channel(s_cap[i].gpio, true, &s_ch_rise);
    if (err == ESP_OK) err = cap_channel(s_cap[i].gpio, false, &s_ch_fall);
    return err;
}

// Print the captured edges as "R/F <us since previous edge>" and stop dumping.
static void dump_print(int i)
{
    portENTER_CRITICAL(&s_cap_mux);
    s_dumping = false;
    s_dump_req = -1;
    int n = s_dump_n;
    portEXIT_CRITICAL(&s_cap_mux);
    // The two channels' interrupts can arrive out of order: sort by time first.
    for (int a = 1; a < n; a++) {
        for (int b = a; b > 0 && (int32_t)(s_dump_t[b] - s_dump_t[b - 1]) < 0; b--) {
            uint32_t t = s_dump_t[b]; s_dump_t[b] = s_dump_t[b - 1]; s_dump_t[b - 1] = t;
            uint8_t r = s_dump_rise[b]; s_dump_rise[b] = s_dump_rise[b - 1]; s_dump_rise[b - 1] = r;
        }
    }
    printf("EDGES %s GPIO%d: %d edges (R/F = rising/falling, us since previous edge)\n",
           i ? "ORP" : "pH", s_cap[i].gpio, n);
    for (int k = 0; k < n; k++) {
        float dt = k ? (float)(s_dump_t[k] - s_dump_t[k - 1]) * 1e6f / s_res_hz : 0.0f;
        printf("%c%.2f%s", s_dump_rise[k] ? 'R' : 'F', dt, (k % 16 == 15) ? "\n" : " ");
    }
    printf("\nEDGES end\n");
}

void probes_dump_edges(int probe) { s_dump_req = probe; }

typedef struct {
    float period_us;
    uint32_t ok, rej;
} cap_stats_t;

// atlas_gravity *_iso_grav::read_voltage(): mV = pulse time (us) * 35.
static float take_mv(cap_chan_t *c, bool *had_pulses, cap_stats_t *st)
{
    portENTER_CRITICAL(&s_cap_mux);
    uint64_t sl = c->sum_low, sp = c->sum_period;
    uint32_t n = c->n_ok, rej = c->n_rej;
    portEXIT_CRITICAL(&s_cap_mux);
    st->ok = n;
    st->rej = rej;
    st->period_us = n ? (float)sp / n * 1e6f / s_res_hz : 0.0f;

    float us;
    if (n == 0) {
        // No usable pulses: the library's rule (pin high = full scale, low = 0).
        us = gpio_get_level(c->gpio) ? 0.0f : 80.0f;   // inverted like the pulses
        *had_pulses = false;
    } else {
        // The value is in the LOW time (2026-11-20, glitch-free edge dumps, same probe):
        // pH 7 buffer high 35.4 / low 44.6 us, pH 4 buffer high 19.6 / low 60.3 us.
        // Low x 35 gives 1561 / 2110 mV, beside the library defaults 1500 / 2030 and
        // rising in acid as it must; high x 35 (the library) gives 1239 / 686, falling.
        us = (float)sl / n * 1e6f / s_res_hz;
        *had_pulses = true;
    }
    return us * 35.0f;
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
    int64_t last_log_us = 0;
    for (;;) {
        float mv[2];
        bool pulses[2];
        cap_stats_t st[2];
        for (int i = 0; i < 2; i++) {               // each pin gets half the sample period
            esp_err_t err = cap_measure(i);
            if (err != ESP_OK) ESP_LOGE(TAG, "capture on GPIO%d: %s", s_cap[i].gpio, esp_err_to_name(err));
            vTaskDelayUntil(&last, pdMS_TO_TICKS(PROBE_SAMPLE_MS / 2));
            mv[i] = take_mv(&s_cap[i], &pulses[i], &st[i]);
            if (s_dumping) dump_print(i);
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (int i = 0; i < 2; i++) hist_push(&s_hist[i], mv[i], pulses[i]);
        xSemaphoreGive(s_lock);
        if (esp_timer_get_time() - last_log_us > 60 * 1000000LL) {   // the library assumes an 80 us period
            last_log_us = esp_timer_get_time();
            ESP_LOGI(TAG, "PWM: pH %.0f mV (period %.2f us, %lu pulses used, %lu rejected), "
                     "ORP %.0f mV (period %.2f us, %lu used, %lu rejected)",
                     mv[0], st[0].period_us, (unsigned long)st[0].ok, (unsigned long)st[0].rej,
                     mv[1], st[1].period_us, (unsigned long)st[1].ok, (unsigned long)st[1].rej);
        }
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

    mcpwm_capture_timer_config_t tcfg = {
        .group_id = 0,
        .clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT,
    };
    esp_err_t err = mcpwm_new_capture_timer(&tcfg, &s_timer);
    if (err != ESP_OK) return err;
    mcpwm_capture_timer_get_resolution(s_timer, &s_res_hz);
    s_min_period_ticks = (uint32_t)((uint64_t)CAP_MIN_PERIOD_US * s_res_hz / 1000000);
    s_glitch_ticks = (uint32_t)((uint64_t)CAP_GLITCH_NS * s_res_hz / 1000000000);
    s_max_period_ticks = (uint32_t)((uint64_t)CAP_MAX_PERIOD_US * s_res_hz / 1000000);
    if ((err = mcpwm_capture_timer_enable(s_timer)) != ESP_OK) return err;
    if ((err = mcpwm_capture_timer_start(s_timer)) != ESP_OK) return err;
    if ((err = cap_measure(0)) != ESP_OK) return err;      // the sampler takes it from here

    xTaskCreate(sampler_task, "probes", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "pH on GPIO%d, ORP on GPIO%d (isolator PWM, %lu Hz capture)", PIN_PH, PIN_ORP,
             (unsigned long)s_res_hz);
    return ESP_OK;
}
