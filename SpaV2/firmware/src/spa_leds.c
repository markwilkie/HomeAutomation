// SmarterSpa LED decoder.
//
// The four LED lines are sampled every 1 ms and debounced (5 consistent
// samples), not edge-interrupt driven: power-up chatter overflowed a 512-entry
// interrupt queue in test_leds. A white pulse shorter than 200 ms is the
// standby blink (~10 ms on this unit). A red/blue/yellow line that was lit
// within the last LED_FLASH_HOLD_MS counts as on, so a flashing colour reads
// the same as a solid one: the manual (p. 10) says a cycle started with Boost
// flashes its salt light instead of holding it solid. A pattern of
// red/blue/yellow must hold for 5 s before it is accepted; white solid means
// the user is in a menu, so the previous state is held until 5 s after it
// ends. Boost confirmations and the power-up sequence never hold still for
// 5 s, so they are ignored by construction; a counted-flash readout with many
// flashes of one colour could briefly pass as that colour's state (flash
// timing not yet measured). All four lines low (all "lit") means the
// SmarterSpa has no power; no lights and no standby blink is also a fault.

#include "spa_leds.h"
#include "spa_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

static const char *TAG = "spa_leds";

enum { L_WHITE, L_RED, L_BLUE, L_YELLOW, L_COUNT };
static const gpio_num_t PINS[L_COUNT] = { PIN_LED_WHITE, PIN_LED_RED, PIN_LED_BLUE, PIN_LED_YELLOW };
static const char *NAMES[L_COUNT] = { "WHITE", "RED", "BLUE", "YELLOW" };

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// Written by the 1 ms sampler, read by the decoder task (under s_mux).
static bool s_on[L_COUNT];
static uint8_t s_run[L_COUNT];          // consecutive samples disagreeing with s_on
static int64_t s_on_since_ms[L_COUNT];
static int64_t s_last_lit_ms[L_COUNT];  // last time each line was lit (flash hold)
static int64_t s_last_blink_ms = -1;
static int64_t s_pattern_since_ms;      // last change of {red, blue, yellow, white-solid}
static uint8_t s_pattern;               // bit0 red, bit1 blue, bit2 yellow, bit3 white solid

static spa_state_t s_state = SPA_UNKNOWN;
static uint32_t s_gen_seconds;          // lifetime, persisted as hours granularity below

static uint8_t pattern_now(int64_t now)
{
    #define ACTIVE(i) (s_on[i] || now - s_last_lit_ms[i] < LED_FLASH_HOLD_MS)
    uint8_t p = (ACTIVE(L_RED) ? 1 : 0) | (ACTIVE(L_BLUE) ? 2 : 0) | (ACTIVE(L_YELLOW) ? 4 : 0);
    #undef ACTIVE
    if (s_on[L_WHITE] && now - s_on_since_ms[L_WHITE] >= LED_BLINK_MAX_MS) p |= 8;
    return p;
}

static void sample_cb(void *arg)
{
    int64_t now = esp_timer_get_time() / 1000;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < L_COUNT; i++) {
        bool lit = gpio_get_level(PINS[i]) == 0;          // active-low
        if (lit == s_on[i]) { s_run[i] = 0; continue; }
        if (++s_run[i] < LED_DEBOUNCE) continue;
        s_run[i] = 0;
        s_on[i] = lit;
        if (lit) {
            s_on_since_ms[i] = now;
        } else if (i == L_WHITE && now - s_on_since_ms[i] < LED_BLINK_MAX_MS) {
            s_last_blink_ms = now;                        // standby blink
        }
    }
    for (int i = 0; i < L_COUNT; i++) if (s_on[i]) s_last_lit_ms[i] = now;
    uint8_t p = pattern_now(now);
    if (p != s_pattern) {
        s_pattern = p;
        s_pattern_since_ms = now;
    }
    portEXIT_CRITICAL(&s_mux);
}

static spa_state_t classify(uint8_t rby, int64_t now, int64_t last_blink)
{
    switch (rby) {
    case 0:
        // Idle. Standby only if the 10 s white blink is actually happening.
        if (last_blink >= 0 && now - last_blink < LED_STANDBY_GAP_MS) return SPA_STANDBY;
        return (now >= LED_STANDBY_GAP_MS) ? SPA_FAULT : SPA_UNKNOWN;
    case 2: return SPA_GEN_OK;
    case 1: return SPA_GEN_SALT_LOW;
    case 4: return SPA_GEN_SALT_HIGH;
    case 5: return SPA_SALT_SHUTDOWN;
    default: return SPA_UNKNOWN;     // e.g. a steady blue+yellow: not a documented state; hold
    }
}

#define NVS_NS "spaleds"

static void hours_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, "gen_s", s_gen_seconds);
    nvs_commit(h);
    nvs_close(h);
}

static void decoder_task(void *arg)
{
    int64_t last_menu_ms = -LED_MENU_HOLD_MS;
    int64_t last_tick_ms = esp_timer_get_time() / 1000;
    uint32_t unsaved_s = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        int64_t now = esp_timer_get_time() / 1000;
        portENTER_CRITICAL(&s_mux);
        uint8_t p = s_pattern;
        int64_t since = s_pattern_since_ms;
        int64_t blink = s_last_blink_ms;
        portEXIT_CRITICAL(&s_mux);

        spa_state_t next = s_state;
        if (p == 15) {
            // All four "lit" = every line at 0 V: the SmarterSpa is unpowered (seen with it
            // unplugged in test_leds). Fault once that has lasted as long as a missed blink.
            if (now - since >= LED_STANDBY_GAP_MS) next = SPA_FAULT;
        } else if (p & 8) {
            last_menu_ms = now;                       // white solid: menu mode, hold
        } else if (now - last_menu_ms >= LED_MENU_HOLD_MS) {
            uint8_t rby = p & 7;
            bool steady = now - since >= LED_STEADY_MS;
            if (rby == 0) {
                // Idle needs no 5 s hold on the colours; standby/fault is decided by the blink timer.
                if (steady || s_state == SPA_UNKNOWN) next = classify(0, now, blink);
            } else if (steady) {
                spa_state_t c = classify(rby, now, blink);
                if (c != SPA_UNKNOWN) next = c;
            }
        }

        if (next != s_state) {
            ESP_LOGI(TAG, "SmarterSpa: %s -> %s", spa_state_name(s_state), spa_state_name(next));
            s_state = next;
        }

        // Electrode-hours: count whole seconds spent generating.
        if (now - last_tick_ms >= 1000) {
            uint32_t secs = (uint32_t)((now - last_tick_ms) / 1000);
            last_tick_ms += secs * 1000;
            if (s_state == SPA_GEN_OK || s_state == SPA_GEN_SALT_LOW || s_state == SPA_GEN_SALT_HIGH) {
                s_gen_seconds += secs;
                unsaved_s += secs;
                if (unsaved_s >= 600) { hours_save(); unsaved_s = 0; }   // NVS write every 10 min of generating
            }
        }
    }
}

esp_err_t spa_leds_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "gen_s", &s_gen_seconds);
        nvs_close(h);
    }

    uint64_t mask = 0;
    for (int i = 0; i < L_COUNT; i++) mask |= 1ULL << PINS[i];
    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,       // a ~45k pull-up would beat the 470k dividers
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;

    int64_t now = esp_timer_get_time() / 1000;
    for (int i = 0; i < L_COUNT; i++) {
        s_on[i] = gpio_get_level(PINS[i]) == 0;
        s_on_since_ms[i] = now;
        s_last_lit_ms[i] = -LED_FLASH_HOLD_MS;
        ESP_LOGI(TAG, "%s (GPIO%d) boot level %s", NAMES[i], PINS[i], s_on[i] ? "ON" : "off");
    }
    s_pattern = pattern_now(now);
    s_pattern_since_ms = now;

    esp_timer_handle_t t;
    const esp_timer_create_args_t targs = { .callback = sample_cb, .name = "spa_leds" };
    if ((err = esp_timer_create(&targs, &t)) != ESP_OK) return err;
    if ((err = esp_timer_start_periodic(t, LED_SAMPLE_US)) != ESP_OK) return err;

    xTaskCreate(decoder_task, "spa_dec", 3072, NULL, 4, NULL);
    ESP_LOGI(TAG, "Electrode hours so far: %lu", (unsigned long)(s_gen_seconds / 3600));
    return ESP_OK;
}

spa_state_t spa_leds_state(void) { return s_state; }
uint32_t spa_leds_generating_hours(void) { return s_gen_seconds / 3600; }

const char *spa_state_name(spa_state_t s)
{
    switch (s) {
    case SPA_UNKNOWN: return "unknown";
    case SPA_STANDBY: return "standby";
    case SPA_GEN_OK: return "generating (salt OK)";
    case SPA_GEN_SALT_LOW: return "generating (salt low)";
    case SPA_GEN_SALT_HIGH: return "generating (salt high)";
    case SPA_SALT_SHUTDOWN: return "shut down (salt over max)";
    case SPA_FAULT: return "fault / no power";
    }
    return "?";
}
