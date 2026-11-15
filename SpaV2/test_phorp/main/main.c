// SpaV2 test_phorp -- read the Atlas Gravity pH / ORP boards through their
// analog isolators (GRV-ISO / SRV-ISO), the way Atlas's atlas_gravity library
// does it, on the ESP32-C6. No Matter / Thread / Wi-Fi.
//
// The isolator outputs a 3.3 V PWM signal (~80 us period). Atlas's isolated
// classes (ph_iso_grav / orp_iso_grav, MIT, Atlas Scientific 2020) read it with
// pulseIn(pin, HIGH, 400) averaged over 3000 pulses and use:
//     mV = average high time (us) * 35
//     no pulses: 80 us if the pin is high, else 0
// Here pulseIn is replaced by MCPWM capture (hardware edge timestamps). The
// pH / ORP conversion below is the library's, unchanged, with its cleared
// (uncalibrated) defaults -- no calibration in this test.

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/mcpwm_cap.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define PIN_PH   GPIO_NUM_4
#define PIN_ORP  GPIO_NUM_5
#define REPORT_MS 2000
#define LIB_PULSES 3000          // atlas_gravity: volt_avg_len (1000) * 3

typedef struct {
    const char *name;
    gpio_num_t gpio;
    uint32_t last_rise;
    bool have_rise;
    // accumulated in the ISR, taken by the report task
    uint64_t sum_high;
    uint64_t sum_period;
    uint32_t n_high;
    uint32_t n_period;
} chan_t;

static chan_t s_ch[2] = {
    { .name = "pH ", .gpio = PIN_PH },
    { .name = "ORP", .gpio = PIN_ORP },
};
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_res_hz;

static bool IRAM_ATTR on_cap(mcpwm_cap_channel_handle_t cap, const mcpwm_capture_event_data_t *e, void *ctx)
{
    chan_t *c = (chan_t *)ctx;
    portENTER_CRITICAL_ISR(&s_mux);
    if (e->cap_edge == MCPWM_CAP_EDGE_POS) {
        if (c->have_rise) {
            c->sum_period += (uint32_t)(e->cap_value - c->last_rise);
            c->n_period++;
        }
        c->last_rise = e->cap_value;
        c->have_rise = true;
    } else if (c->have_rise) {
        c->sum_high += (uint32_t)(e->cap_value - c->last_rise);
        c->n_high++;
    }
    portEXIT_CRITICAL_ISR(&s_mux);
    return false;
}

// atlas_gravity ph_grav.cpp read_ph(), cleared defaults from cal_clear()
static float lib_ph(float mv)
{
    const float mid = 1500, low = 2030, high = 975;
    if (mv > mid) return 7.0f - 3.0f / (low - mid) * (mv - mid);
    return 7.0f - 3.0f / (mid - high) * (mv - mid);
}

// atlas_gravity orp_grav.cpp read_orp(), cal_offset = 0
static float lib_orp(float mv) { return mv - 1500.0f; }

static void report_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(REPORT_MS));
        int64_t t_ms = esp_timer_get_time() / 1000;
        printf("%8" PRId64 " ", t_ms);
        for (int i = 0; i < 2; i++) {
            chan_t *c = &s_ch[i];
            portENTER_CRITICAL(&s_mux);
            uint64_t sh = c->sum_high, sp = c->sum_period;
            uint32_t nh = c->n_high, np = c->n_period;
            c->sum_high = c->sum_period = 0;
            c->n_high = c->n_period = 0;
            portEXIT_CRITICAL(&s_mux);

            float high_us, period_us = 0;
            if (nh == 0) {
                // library: no pulses -> 80 if pin high, else 0
                high_us = gpio_get_level(c->gpio) ? 80.0f : 0.0f;
            } else {
                high_us = (float)sh / nh * 1e6f / s_res_hz;
            }
            if (np) period_us = (float)sp / np * 1e6f / s_res_hz;
            float mv = high_us * 35.0f;
            float duty = period_us > 0 ? 100.0f * high_us / period_us : (nh == 0 && high_us > 0 ? 100.0f : 0.0f);

            printf(" | %s high=%6.2fus period=%6.2fus (%5.1f%%, %" PRIu32 " pulses) mV=%7.1f -> ",
                   c->name, high_us, period_us, duty, nh, mv);
            if (i == 0) printf("pH %5.2f", lib_ph(mv));
            else        printf("ORP %+7.1f mV", lib_orp(mv));
            if (nh == 0) printf(" [NO PULSES, pin %s]", gpio_get_level(c->gpio) ? "high" : "low");
            else if (nh < LIB_PULSES) printf(" [only %" PRIu32 " pulses, lib averages %d]", nh, LIB_PULSES);
        }
        printf("\n");
    }
}

void app_main(void)
{
    printf("\nSpaV2 test_phorp -- Atlas Gravity pH (GPIO%d) / ORP (GPIO%d) via isolator PWM\n", PIN_PH, PIN_ORP);
    printf("mV = avg high time (us) x 35 (atlas_gravity iso). pH/ORP use library defaults, uncalibrated.\n");
    printf("Expect period ~80 us. Report every %d s.\n\n", REPORT_MS / 1000);

    mcpwm_cap_timer_handle_t timer;
    mcpwm_capture_timer_config_t tcfg = {
        .group_id = 0,
        .clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT,
    };
    ESP_ERROR_CHECK(mcpwm_new_capture_timer(&tcfg, &timer));
    ESP_ERROR_CHECK(mcpwm_capture_timer_get_resolution(timer, &s_res_hz));
    printf("Capture timer resolution: %" PRIu32 " Hz (%.1f ns)\n\n", s_res_hz, 1e9 / s_res_hz);

    for (int i = 0; i < 2; i++) {
        mcpwm_cap_channel_handle_t ch;
        mcpwm_capture_channel_config_t ccfg = {
            .gpio_num = s_ch[i].gpio,
            .prescale = 1,
            .flags.pos_edge = true,
            .flags.neg_edge = true,
            // no pulls: the isolator drives the line
        };
        ESP_ERROR_CHECK(mcpwm_new_capture_channel(timer, &ccfg, &ch));
        mcpwm_capture_event_callbacks_t cbs = { .on_cap = on_cap };
        ESP_ERROR_CHECK(mcpwm_capture_channel_register_event_callbacks(ch, &cbs, &s_ch[i]));
        ESP_ERROR_CHECK(mcpwm_capture_channel_enable(ch));
    }
    ESP_ERROR_CHECK(mcpwm_capture_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_capture_timer_start(timer));

    xTaskCreate(report_task, "report", 4096, NULL, 5, NULL);
}
