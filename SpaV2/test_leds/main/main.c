// SpaV2 test_leds -- verify the 4 SmarterSpa LED lines reach the ESP32-C6.
//
// No Matter / Thread / Wi-Fi. Prints every LED transition and a 10 s summary.
// Pin map is PLAN.md's table, inferred from wire colours. This app does NOT
// correct a wrong mapping: if the power-up order prints differently from
// W,W,(W,R,B,G), the pins in the table below are swapped -- fix PLAN.md and
// the wiring/firmware deliberately, not here.
//
// Electrical: each line is 5 V through a 56k/100k divider, active-low
// (LED lit = line LOW). No internal pulls -- the divider sets the level.

#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

typedef struct {
    gpio_num_t gpio;
    const char *name;
} led_pin_t;

static const led_pin_t PINS[] = {
    // Order as wired on the board (2026-11-14): reversed vs. the
    // original PLAN.md table.
    { GPIO_NUM_19, "GREEN" },  // ribbon 7, yellow wire
    { GPIO_NUM_20, "BLUE"  },  // ribbon 6, blue wire
    { GPIO_NUM_21, "RED"   },  // ribbon 5, red wire
    { GPIO_NUM_22, "WHITE" },  // ribbon 1, white wire
};
#define NUM_PINS (sizeof(PINS) / sizeof(PINS[0]))

#define SUMMARY_PERIOD_MS   10000
#define CHATTER_WINDOW_US   100000   // 100 ms
#define CHATTER_EDGES       20       // > this many edges in the window = chatter
#define QUEUE_LEN           512

typedef struct {
    int64_t t_us;
    uint8_t idx;
    uint8_t level;
} edge_evt_t;

static QueueHandle_t s_queue;
static volatile uint32_t s_dropped;   // ISR could not enqueue (queue full)

static void gpio_isr(void *arg)
{
    uint32_t idx = (uint32_t)arg;
    edge_evt_t evt = {
        .t_us = esp_timer_get_time(),
        .idx = (uint8_t)idx,
        .level = (uint8_t)gpio_get_level(PINS[idx].gpio),
    };
    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(s_queue, &evt, &woken) != pdTRUE) {
        s_dropped++;
    }
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

static inline bool is_on(int level) { return level == 0; }  // active-low

typedef struct {
    bool on;                 // last known state
    int64_t on_since_us;     // when it turned on (valid while on)
    uint32_t transitions;    // state changes since last summary
    uint32_t edges;          // raw interrupts since last summary
    uint32_t same_level;     // interrupts that didn't change the sampled level
    int64_t win_start_us;    // chatter window
    uint32_t win_edges;
    bool win_warned;
} pin_state_t;

static pin_state_t s_state[NUM_PINS];

static void print_levels(const char *label)
{
    printf("%s:", label);
    for (int i = 0; i < NUM_PINS; i++) {
        int lvl = gpio_get_level(PINS[i].gpio);
        printf("  %s(GPIO%d)=%d[%s]", PINS[i].name, PINS[i].gpio, lvl, is_on(lvl) ? "ON" : "off");
    }
    printf("\n");
}

static void handle_edge(const edge_evt_t *e)
{
    pin_state_t *s = &s_state[e->idx];
    const char *name = PINS[e->idx].name;
    s->edges++;

    // Chatter detection: count raw edges in a rolling-reset 100 ms window.
    if (e->t_us - s->win_start_us > CHATTER_WINDOW_US) {
        s->win_start_us = e->t_us;
        s->win_edges = 0;
        s->win_warned = false;
    }
    s->win_edges++;
    bool chattering = s->win_edges > CHATTER_EDGES;
    if (chattering && !s->win_warned) {
        s->win_warned = true;
        printf("%" PRId64 "  WARNING: %s (GPIO%d) chattering: >%d edges in 100 ms -- "
               "divider/level likely marginal near the input threshold; measure the "
               "line at the GPIO (see PLAN.md). Suppressing per-edge prints for this window.\n",
               e->t_us / 1000, name, PINS[e->idx].gpio, CHATTER_EDGES);
    }

    bool on = is_on(e->level);
    if (on == s->on) {
        // Edge fired but the level read back unchanged: a pulse shorter than
        // ISR latency, or a bounce. Count it; don't fake a transition.
        s->same_level++;
        return;
    }

    s->on = on;
    s->transitions++;
    if (chattering) {
        if (on) s->on_since_us = e->t_us;
        return;
    }
    if (on) {
        s->on_since_us = e->t_us;
        printf("%8" PRId64 "  %s ON\n", e->t_us / 1000, name);
    } else {
        printf("%8" PRId64 "  %s OFF (on for %" PRId64 " ms)\n",
               e->t_us / 1000, name, (e->t_us - s->on_since_us) / 1000);
    }
}

static void print_summary(void)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    printf("%8" PRId64 "  --- 10 s summary:", now_ms);
    for (int i = 0; i < NUM_PINS; i++) {
        pin_state_t *s = &s_state[i];
        printf("  %s=%s tr=%" PRIu32, PINS[i].name, s->on ? "ON" : "off", s->transitions);
        if (s->same_level) printf(" (+%" PRIu32 " no-change edges)", s->same_level);
        if (s->transitions == 0) printf(" <-- NO TRANSITIONS");
    }
    uint32_t dropped = s_dropped;
    if (dropped) {
        s_dropped = 0;
        printf("  | %" PRIu32 " events DROPPED (queue full)", dropped);
    }
    printf("\n");

    // Re-sync from the pins in case an edge was missed/dropped.
    for (int i = 0; i < NUM_PINS; i++) {
        pin_state_t *s = &s_state[i];
        bool on = is_on(gpio_get_level(PINS[i].gpio));
        if (on != s->on) {
            printf("          note: %s state resynced from pin (%s -> %s)\n",
                   PINS[i].name, s->on ? "ON" : "off", on ? "ON" : "off");
            s->on = on;
            if (on) s->on_since_us = esp_timer_get_time();
        }
        s->transitions = 0;
        s->edges = 0;
        s->same_level = 0;
    }
}

// Prints the hardware IO_MUX state of the 4 pins (pull-up/pull-down, input
// enable, ...) straight from the registers, to prove the pulls are really off.
// The 470k dividers are weak enough that a ~45k internal pull-up would win.
static void dump_pin_config(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < NUM_PINS; i++) mask |= 1ULL << PINS[i].gpio;
    gpio_dump_io_configuration(stdout, mask);
    print_levels("Levels");
}

static void led_task(void *arg)
{
    TickType_t next_summary = xTaskGetTickCount() + pdMS_TO_TICKS(SUMMARY_PERIOD_MS);
    uint32_t summaries = 0;
    edge_evt_t e;
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        TickType_t wait = (int32_t)(next_summary - now) > 0 ? next_summary - now : 0;
        if (xQueueReceive(s_queue, &e, wait) == pdTRUE) {
            handle_edge(&e);
        }
        if ((int32_t)(xTaskGetTickCount() - next_summary) >= 0) {
            print_summary();
            // Boot output is usually lost while USB re-enumerates after a
            // flash, so repeat the pin-config dump every 60 s.
            if (++summaries % 6 == 1) dump_pin_config();
            next_summary += pdMS_TO_TICKS(SUMMARY_PERIOD_MS);
        }
    }
}

void app_main(void)
{
    printf("\nSpaV2 test_leds -- SmarterSpa LED line check (active-low: 0 = LED lit)\n");
    printf("Expected on SmarterSpa power-up: WHITE x2, then WHITE, RED, BLUE, GREEN one at a time,\n"
           "then a single short WHITE flash every 10 s (standby).\n\n");

    s_queue = xQueueCreate(QUEUE_LEN, sizeof(edge_evt_t));

    uint64_t mask = 0;
    for (int i = 0; i < NUM_PINS; i++) mask |= 1ULL << PINS[i].gpio;
    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    print_levels("Boot levels");
    dump_pin_config();
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < NUM_PINS; i++) {
        s_state[i].on = is_on(gpio_get_level(PINS[i].gpio));
        s_state[i].on_since_us = now;
    }

    xTaskCreate(led_task, "led_task", 4096, NULL, 10, NULL);

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    for (int i = 0; i < NUM_PINS; i++) {
        ESP_ERROR_CHECK(gpio_isr_handler_add(PINS[i].gpio, gpio_isr, (void *)(uint32_t)i));
    }
    printf("Watching GPIO19/20/21/22 (any edge). Summary every %d s.\n\n", SUMMARY_PERIOD_MS / 1000);
}
