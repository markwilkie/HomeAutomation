#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include <time.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "tuya_client.h"
#include "matter_device.h"
#include "papertrail_logger.h"
#include "flash_log.h"
#include "log_server.h"
#include "outage_log.h"
#include "ir_tcl112.h"
#include "control_logic.h"
#include "secrets.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_app_desc.h"
#include <openthread/instance.h>
#include <openthread/thread.h>

static const char *TAG = "MAIN";


// Set once Matter's network layer (Thread) reports connectivity -- see
// matter_set_network_event_group() / app_chip_event_handler() in
// matter_device.cpp. Network bring-up is owned by Matter's commissioning
// flow now, not app-level pre-connect code, so this can take anywhere from
// a few seconds (already-commissioned reboot) to indefinitely (first-time
// commissioning, waiting on the user).
#define NETWORK_CONNECTED_BIT BIT0

static EventGroupHandle_t g_app_event_group = NULL;
static tuya_device_status_t g_last_device_status = {0};
static bool g_last_device_status_valid = false;

// Written by sync_task and command_task, read by those plus followme_task --
// copy in/out under this lock so a reader never sees a half-written struct
// (2026-10-05). Readers work on their own copy (status_cache_get()).
static portMUX_TYPE g_status_cache_lock = portMUX_INITIALIZER_UNLOCKED;

static bool status_cache_get(tuya_device_status_t *out)
{
    taskENTER_CRITICAL(&g_status_cache_lock);
    *out = g_last_device_status;
    bool valid = g_last_device_status_valid;
    taskEXIT_CRITICAL(&g_status_cache_lock);
    return valid;
}

// Serializes every call into transmit_ir_state_frame() across
// command_task/sync_task/followme_task -- see that function's doc comment.
// Created in app_main() before any of those tasks start.
static SemaphoreHandle_t g_ir_send_mutex = NULL;

// Timing configuration
//
// Status polling used to be adaptive (fast for a while after any command,
// slow otherwise), keyed off a "was a command recently sent" timestamp. That
// stopped mattering once the expectation/revert machinery it supported was
// removed: nothing downstream depends on catching a confirmation quickly
// anymore (see sync_task's desired-setpoint reconciliation, which just
// checks again on every poll regardless of timing), and the trigger never
// even sped up picking up a *new* desired-setpoint change in the first
// place, since it only fired after a command had already gone out. All it
// actually traded off was display freshness against Tuya Cloud API quota
// usage -- so it's now a single fixed interval instead, chosen on the quota
// side of that tradeoff (this is "the dominant contributor to Tuya Cloud API
// quota usage" per the original comment here): a new desired-setpoint value
// may sit unpicked-up for up to this long, which is an accepted tradeoff
// given nothing about correctness depends on it landing faster.
// Same Papertrail account already used by Sprinter/PowerMonitor and
// Sprinter/TripDisplay elsewhere in this repo (one syslog endpoint per
// account; "system name" distinguishes devices in the Papertrail UI) --
// see https://my.papertrailapp.com/systems/minisplit/events
#define PAPERTRAIL_HOST       "logs4.papertrailapp.com"
#define PAPERTRAIL_PORT       54449
#define PAPERTRAIL_SYSTEMNAME "minisplit"

#define STATUS_POLL_INTERVAL_MS 300000    // Poll Tuya every 5 minutes, fixed
#define COMMAND_POLL_INTERVAL_MS 5000     // Check Matter commands every 5 seconds

// Gap between the Type2 companion send and the Type1 send in
// transmit_ir_state_frame() -- matches the real remote's observed spacing
// (measured ~69841-69855us across multiple raw-pin captures, 2026-10-01;
// see MiniSplitIR/capture_tools/remote_capture vs esp_capture). Previously
// 0 (no delay at all between the two sends).
#define IR_TCL112_INTER_FRAME_GAP_MS 70
#define RETRY_DELAY_MS 2000               // Base delay before retry on error (doubles per attempt)
#define MAX_RETRIES 3                     // Retry up to 3 times before giving up

// Self-restart threshold for a stalled sync_task, checked by health_task.
// Not routed through ESP-IDF's Task Watchdog Timer: that's shared with
// idle-task-starvation detection and tuned for a short (default ~5s)
// timeout, appropriate for tasks that never legitimately block for long --
// subscribing a task that sleeps for a full STATUS_POLL_INTERVAL_MS between
// polls would either trip constantly (spurious reboots) or require
// retuning the *global* timeout, affecting every other watched task too.
// This is a dedicated, scoped check instead: g_sync_state.last_status_update
// already gets stamped on every successful poll (see sync_task) and was
// already being logged every 60s by health_task, just never acted on. Set
// well above any expected normal cycle time -- even a run of consecutive
// failures now bounded by TUYA_HTTP_TIMEOUT_MS (tuya_client.c) plus
// MAX_RETRIES backoff totals well under a minute per poll -- so tripping
// this means something hung in a way that timeout couldn't catch, not
// ordinary transient Tuya API flakiness.
#define SYNC_STALL_RESTART_MS (20 * 60 * 1000)  // 20 minutes

// State tracking for error recovery
typedef struct {
    uint32_t last_status_update;        // Timestamp of last successful status update
    uint32_t last_command_check;        // Timestamp of last command check
    uint8_t status_poll_failures;       // Consecutive failures
    uint8_t network_disconnects;        // Count of connectivity drops
} sync_state_t;

static sync_state_t g_sync_state = {0};

// Shared by every tuya_get_device_status() call site (sync_task's own poll,
// post_send_verify_and_sync(), and command_task's three pre-send refreshes)
// -- added 2026-09-09. Before this, only sync_task's own poll ever cleared
// OUTAGE_REASON_TUYA_UNREACHABLE or reset status_poll_failures, so a real
// command's pre-send refresh (or post-send verify) could succeed --
// definitive proof Tuya is reachable, and HA would show fresh values -- while
// the outage stayed "active" until sync_task's own next scheduled poll
// happened to succeed too, up to 5 minutes later. Any successful poll from
// anywhere should count.
static void note_tuya_poll_success(void)
{
    if (g_sync_state.status_poll_failures > 0) {
        outage_log_end(OUTAGE_REASON_TUYA_UNREACHABLE);
    }
    g_sync_state.status_poll_failures = 0;
}

// PLAN.md Milestone 4: the Tuya command paths this used to step through
// (tuya_set_temperature() et al.) are retired -- send_ir_frame() now sends
// the exact desired value directly in one shot, every time, from every call
// site (Desired Setpoint change, System Mode change, OnOff, and sync_task's
// mismatch reconciliation below). No gradual per-cycle stepping is needed
// for IR the way Tuya's own control loop needed it: a real remote press
// just sets the target state directly, same as this driver now does.

// The product spec claims compressor_frequency is x10-scaled (max 1500 = 150.0Hz),
// but live readings show it reporting the same raw, unscaled Hz value as
// outdoor_comptar_freqrun (max 150) -- e.g. both read 14 simultaneously. Treat it
// as raw Hz to match observed behavior rather than the spec's stated scale.
//
// 150Hz is the shared product-family typeSpec ceiling, not this unit's real
// operating range. Calibrated instead against this unit's observed bands
// (idle/low ~15-25Hz, steady-state rated cooling ~50-70Hz, boost/turbo
// ~90-130Hz, occasionally spiking toward 150Hz briefly) so the resulting
// percentage reads intuitively: idle/low ~12-19%, steady-state ~38-54%,
// boost/turbo ~69-100%. Rare spikes above 130Hz simply clip at 100%.
#define COMPRESSOR_FREQUENCY_MAX 130

static uint8_t compressor_demand_percent(const tuya_device_status_t *device_status)
{
    int32_t freq = device_status->compressor_frequency;
    if (freq <= 0) {
        return 0;
    }
    if (freq > COMPRESSOR_FREQUENCY_MAX) {
        freq = COMPRESSOR_FREQUENCY_MAX;
    }
    return (uint8_t)((freq * 100) / COMPRESSOR_FREQUENCY_MAX);
}

static const char *ac_mode_name(uint8_t ac_mode)
{
    static const char *const names[] = {"Auto", "Cool", "Dry", "Fan", "Heat"};
    return (ac_mode < (sizeof(names) / sizeof(names[0]))) ? names[ac_mode] : "Unknown";
}


// Mode reconciliation (2026-10-04) -- sync_task compares the Desired
// Setpoint endpoint's mode (matter_get_desired_system_mode()) against the
// unit's reported mode every poll, the way it already did for setpoint.
// Before this nothing reconciled mode in either direction: the old
// unit->Desired mirror was removed 2026-10-01 (it snapped a fresh "Off" back
// to the stale prior mode while Tuya lagged) and there was never a
// Desired->unit resend, so a missed command or an out-of-band change stayed
// mismatched for hours (2026-10-03: unit off/auto, Desired heat, ~6h).
//
// Which side wins depends on which one moved last. If the unit changed after
// the last Desired change and the new mode isn't something we commanded in
// the last MODE_ECHO_WINDOW_MS, it was changed out-of-band (IR remote, Tuya
// app) and Desired adopts it. Otherwise (a command that didn't land, or a
// late Tuya report echoing an older command of ours -- observed ~15 min
// late 2026-10-01) Desired's mode is resent. Requires the mismatch to hold
// for MODE_MISMATCH_POLLS consecutive polls, and won't resend a mode it
// already sent within MODE_RESEND_HOLDOFF_MS, so Tuya's reporting lag
// doesn't cause duplicate sends.
#define MODE_ECHO_WINDOW_MS (30 * 60 * 1000)
#define MODE_RESEND_HOLDOFF_MS (15 * 60 * 1000)
#define MODE_CMD_HISTORY_LEN 4

typedef struct {
    uint8_t matter_mode;
    TickType_t tick;
    bool valid;
} mode_cmd_record_t;

static mode_cmd_record_t g_mode_cmd_history[MODE_CMD_HISTORY_LEN];
static uint8_t g_mode_cmd_history_next = 0;
static TickType_t g_desired_mode_changed_tick = 0;
static uint8_t g_last_actual_mode = MATTER_DESIRED_MODE_UNKNOWN;
static TickType_t g_actual_mode_changed_tick = 0;
static uint8_t g_mode_mismatch_polls = 0;

// command_task writes the history, sync_task reads it.
static portMUX_TYPE g_mode_cmd_history_lock = portMUX_INITIALIZER_UNLOCKED;

static void record_mode_command(uint8_t matter_mode)
{
    TickType_t now = xTaskGetTickCount();
    taskENTER_CRITICAL(&g_mode_cmd_history_lock);
    g_mode_cmd_history[g_mode_cmd_history_next] = (mode_cmd_record_t){
        .matter_mode = matter_mode, .tick = now, .valid = true};
    g_mode_cmd_history_next = (uint8_t)((g_mode_cmd_history_next + 1) % MODE_CMD_HISTORY_LEN);
    taskEXIT_CRITICAL(&g_mode_cmd_history_lock);
}

static bool mode_commanded_within(uint8_t matter_mode, uint32_t window_ms)
{
    TickType_t now = xTaskGetTickCount();
    bool found = false;
    taskENTER_CRITICAL(&g_mode_cmd_history_lock);
    for (int i = 0; i < MODE_CMD_HISTORY_LEN; i++) {
        const mode_cmd_record_t *r = &g_mode_cmd_history[i];
        if (r->valid && r->matter_mode == matter_mode && (now - r->tick) < pdMS_TO_TICKS(window_ms)) {
            found = true;
            break;
        }
    }
    taskEXIT_CRITICAL(&g_mode_cmd_history_lock);
    return found;
}

// First observation after boot is not treated as a change (tick stays 0), so
// a boot-time mismatch resolves in Desired's favor rather than adopting.
static void note_actual_mode(uint8_t matter_mode)
{
    if (g_last_actual_mode != MATTER_DESIRED_MODE_UNKNOWN && matter_mode != g_last_actual_mode) {
        g_actual_mode_changed_tick = xTaskGetTickCount();
    }
    g_last_actual_mode = matter_mode;
}

static void note_desired_mode_changed(void)
{
    g_desired_mode_changed_tick = xTaskGetTickCount();
    g_mode_mismatch_polls = 0;
}



// Sends the Type 2 companion frame + the given Type 1 frame (already built
// by build_ir_state_frame() or send_followme_frame()), logging both. Shared
// by send_ir_frame() and send_followme_frame() -- every full-state send
// needs this same two-frame sequence (see IR_PROTOCOL_REFERENCE.md's "Type 2
// frame" section: confirmed required, not optional, via test_apps/ir_live_test).
//
// `fresh_air_on` drives the Type 2 frame's state[12] bit 0x01 -- found via
// bit-level capture comparison (2026-09-10): the real remote's Type 2 frame
// carries state[12]=0x01 while Fresh Air is on, 0x00 while off (6 captures,
// 3 each, all checksum-valid, Type 1 unaffected). Before this fix, this
// frame was a fixed template with that bit always 0, so every command this
// firmware sent silently told the unit Fresh Air was off -- the root cause
// of Fresh Air reverting on any Device B command. See
// IR_PROTOCOL_REFERENCE.md's "Known gaps" for the full writeup.
// Fresh Air switch (2026-10-04). Every full-state frame carries the Fresh
// Air bit, so while a Fresh Air command is still waiting for Tuya to report
// it (lag observed up to ~15 min), every send uses the commanded value
// instead of the stale reported one -- otherwise the next Follow-Me
// heartbeat would revert the change. After the window, Tuya's reported
// state is trusted again (so a remote/app change sticks).
static bool g_fresh_air_cmd_valid = false;
static bool g_fresh_air_cmd_value = false;
static TickType_t g_fresh_air_cmd_tick = 0;

static bool effective_fresh_air(const tuya_device_status_t *status)
{
    uint32_t ms_since_cmd = pdTICKS_TO_MS(xTaskGetTickCount() - g_fresh_air_cmd_tick);
    return fresh_air_effective(g_fresh_air_cmd_valid, g_fresh_air_cmd_value, ms_since_cmd, status->fresh_air_valve);
}

static esp_err_t transmit_ir_state_frame(uint8_t frame[IR_TCL112_FRAME_LEN], bool fresh_air_on)
{
    // Serializes every IR send across command_task, sync_task, and
    // followme_task -- 2026-10-01, found via user question while reviewing
    // the IR_TCL112_INTER_FRAME_GAP_MS change above: nothing previously
    // prevented two of those tasks from calling this function concurrently
    // on the same shared RMT TX channel. Before the inter-frame gap existed
    // (~1-2ms between the Type2 and Type1 sends), the collision window was
    // narrow; the 70ms gap widens it enough that a second task's Type2+Type1
    // pair could plausibly interleave with the first's mid-transmission,
    // corrupting both. portMAX_DELAY is safe here: every call site already
    // expects this function to block for the real transmission time anyway
    // (rmt_tx_wait_all_done() below), so waiting for another task's turn is
    // the same category of wait, not a new failure mode.
    if (xSemaphoreTake(g_ir_send_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire IR send mutex");
        return ESP_FAIL;
    }

    // state[6] is a real capture-confirmed free-running step counter that
    // doesn't gate acceptance (see ../MiniSplitIR/captures/protocol_capture.md),
    // so this fixed, verbatim real capture is enough -- no need to reproduce
    // its exact sequence. state[12] (Fresh Air) is the one byte overwritten
    // below rather than left as part of this captured template.
    static const uint8_t kType2CompanionFrame[IR_TCL112_FRAME_LEN] = {
        0x23, 0xCB, 0x26, 0x02, 0x00, 0x40, 0x20, 0x00, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x48,
    };
    uint8_t type2_frame[IR_TCL112_FRAME_LEN];
    memcpy(type2_frame, kType2CompanionFrame, IR_TCL112_FRAME_LEN);
    if (fresh_air_on) {
        type2_frame[12] |= 0x01;
    } else {
        type2_frame[12] &= (uint8_t)~0x01;
    }

    // Log the exact bytes about to go out, before ir_tcl112_send() mutates
    // frame[13]/type2_frame[13] with the freshly computed checksum -- that's
    // the only byte it ever touches, so this is still the real on-air
    // content up to the checksum.
    ESP_LOGI(TAG, "Type2 frame: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             type2_frame[0], type2_frame[1], type2_frame[2], type2_frame[3], type2_frame[4],
             type2_frame[5], type2_frame[6], type2_frame[7], type2_frame[8], type2_frame[9],
             type2_frame[10], type2_frame[11], type2_frame[12], type2_frame[13]);
    ESP_LOGI(TAG, "Type1 frame: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             frame[0], frame[1], frame[2], frame[3], frame[4], frame[5], frame[6], frame[7],
             frame[8], frame[9], frame[10], frame[11], frame[12], frame[13]);

    esp_err_t type2_err = ir_tcl112_send(type2_frame);
    if (type2_err != ESP_OK) {
        ESP_LOGE(TAG, "ir_tcl112_send (Type2 companion) failed: %s", esp_err_to_name(type2_err));
        xSemaphoreGive(g_ir_send_mutex);
        return type2_err;
    }

    // Inter-frame gap -- 2026-10-01: raw-pin capture comparison against the
    // real remote (MiniSplitIR/capture_tools/esp_capture vs remote_capture)
    // found the real remote leaves ~70ms of silence between the Type2 and
    // Type1 sends (measured ~69841-69855us across multiple captures,
    // consistently); this function previously had nothing here at all, so
    // the two sends went out back-to-back (~1-2ms apart, just incidental
    // call overhead). IR_TCL112_GAP_US in ir_tcl112.c is a *trailing footer*
    // gap after a single frame's last bit, never wired up as an inter-frame
    // gap -- this is a separate, new delay. NOT yet confirmed whether this
    // gap is what the real unit's receiver actually needs to treat the two
    // frames as distinct rather than one run-on transmission -- re-verify
    // against real hardware behavior, not just remote-timing matching, once
    // flashed.
    vTaskDelay(pdMS_TO_TICKS(IR_TCL112_INTER_FRAME_GAP_MS));

    esp_err_t err = ir_tcl112_send(frame);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ir_tcl112_send failed: %s", esp_err_to_name(err));
    }
    xSemaphoreGive(g_ir_send_mutex);
    return err;
}

static bool g_followme_active;

// Last setpoint the bridge sent (whole F) and when -- lets sync_task tell its
// own command landing from a remote/app change (setpoint_reconcile_decide()).
static portMUX_TYPE g_setpoint_sent_lock = portMUX_INITIALIZER_UNLOCKED;
static bool g_setpoint_sent_valid;
static int16_t g_setpoint_sent_f;
static TickType_t g_setpoint_sent_tick;

static void note_setpoint_sent(int16_t setpoint_c_x100)
{
    taskENTER_CRITICAL(&g_setpoint_sent_lock);
    g_setpoint_sent_f = tuya_setpoint_c_to_f(setpoint_c_x100);
    g_setpoint_sent_tick = xTaskGetTickCount();
    g_setpoint_sent_valid = true;
    taskEXIT_CRITICAL(&g_setpoint_sent_lock);
}

static bool setpoint_last_sent(int16_t *out_f, uint32_t *out_ms_ago)
{
    taskENTER_CRITICAL(&g_setpoint_sent_lock);
    bool valid = g_setpoint_sent_valid;
    *out_f = g_setpoint_sent_f;
    *out_ms_ago = pdTICKS_TO_MS(xTaskGetTickCount() - g_setpoint_sent_tick);
    taskEXIT_CRITICAL(&g_setpoint_sent_lock);
    return valid;
}

// Ambient reading HA last relayed, rounded to the whole degrees C state[11]
// carries (see IR_PROTOCOL_REFERENCE.md's Follow Me section).
static bool followme_ambient_whole_c(int8_t *out_c)
{
    int16_t ambient_c_x100;
    if (!matter_get_followme_ambient_temp_c_x100(&ambient_c_x100)) {
        return false;
    }
    *out_c = (int8_t)((ambient_c_x100 >= 0 ? ambient_c_x100 + 50 : ambient_c_x100 - 50) / 100);
    return true;
}

// See build_ir_state_frame()'s doc comment for the base-template/known-
// limitation notes that apply here too. Callers are responsible for
// refreshing `status` from a fresh, on-demand Tuya GET immediately
// beforehand (see command_task) -- this function only builds and sends.
//
// 2026-10-04: while Follow-Me is active, command frames carry its bits and
// the current ambient reading too (heartbeat-style, state[5] 0x20 clear).
// A frame without them is byte-for-byte the real remote's Follow-Me
// *disable* frame (IR_PROTOCOL_REFERENCE.md's "Follow Me behavior"), so every
// setpoint/mode/Fresh Air command and sync_task correction used to switch
// the unit back to its own onboard sensor -- which read ~73F against
// Follow-Me's ~73F room on 2026-10-04 (75-77F reported) -- until the next
// heartbeat re-engaged it, up to 3 minutes later.
// Power-off frames are left plain (the unit is turning off anyway).
static esp_err_t send_ir_frame(const tuya_device_status_t *status, bool power_on,
                           bool override_mode, uint8_t override_ir_mode,
                           bool override_setpoint, int16_t override_setpoint_c_x100)
{
    uint8_t frame[IR_TCL112_FRAME_LEN];
    uint8_t ir_mode;
    int16_t setpoint_c;
    build_ir_state_frame(status, power_on, override_mode, override_ir_mode,
                          override_setpoint, override_setpoint_c_x100,
                          frame, &ir_mode, &setpoint_c);

    int8_t ambient_c = 0;
    bool with_followme = power_on && g_followme_active && followme_ambient_whole_c(&ambient_c);
    if (with_followme) {
        ir_frame_set_followme(frame, ambient_c);
    }

    esp_err_t err = transmit_ir_state_frame(frame, effective_fresh_air(status));
    if (err == ESP_OK && power_on && override_setpoint) {
        note_setpoint_sent(override_setpoint_c_x100);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "IR frame sent: power=%s mode=%u setpoint=%dC follow_me=%s",
                 power_on ? "on" : "off", ir_mode, setpoint_c, with_followme ? "kept" : "off");
    }
    return err;
}

// Follow-Me heartbeat frame (PLAN.md Milestone 3). Reflects the same
// Power/Mode/Setpoint/Light as a regular command (via build_ir_state_frame(),
// no overrides -- Follow-Me doesn't change any of those, just layers its own
// bits on top), plus:
//   - state[4]/state[6] bit 0x80: Follow-Me enabled (state[6]'s copy is a
//     real capture-confirmed mirror of state[4]'s, not independently
//     meaningful on its own).
//   - state[5] bit 0x20 clear: heartbeat, not the remote's beeping "enable"
//     instance -- see IR_PROTOCOL_REFERENCE.md's "Follow Me behavior".
//   - state[11]: ambient sensor temp, whole degrees C.
// 2026-10-05: always a heartbeat. The enable instance (on boot, after any
// data gap, and a forced one every 8 hours) was removed after confirming a
// plain heartbeat turns Follow-Me on by itself (2026-10-04, three times after
// disable-shaped frames; IR_PROTOCOL_REFERENCE.md) -- so the 3-minute
// heartbeat already re-asserts it continuously, and the enable instance only
// added beeps.
//
// g_followme_active: a heartbeat has gone out since the last gap (no
// sensor reading, unit off). send_ir_frame() only carries Follow-Me bits in
// command frames while this is set.
static bool g_followme_active = false;

// Timestamp of the last successful Follow-Me send specifically -- 2026-10-01:
// previously shared with every IR send of any
// kind (command or heartbeat) via a single g_last_ir_send_tick, which meant
// an unrelated command sent from command_task reset followme_task's 3-minute
// clock, delaying the next heartbeat by a full interval every time one fired
// (originally intentional, PLAN.md Milestone 2's "don't immediately follow a
// just-sent command" note). Changed on request: a real heartbeat needs to go
// out every 3 minutes regardless of other traffic -- repeatedly pushing it
// back risks the AC's own internal Follow-Me fallback timeout lapsing if
// commands happen to arrive more often than every 3 minutes.
static TickType_t g_last_followme_send_tick = 0;

static esp_err_t send_followme_frame(const tuya_device_status_t *status, int8_t ambient_temp_c)
{
    uint8_t frame[IR_TCL112_FRAME_LEN];
    uint8_t ir_mode;
    int16_t setpoint_c;
    build_ir_state_frame(status, true, false, 0, false, 0, frame, &ir_mode, &setpoint_c);
    ir_frame_set_followme(frame, ambient_temp_c);

    esp_err_t err = transmit_ir_state_frame(frame, effective_fresh_air(status));
    if (err == ESP_OK) {
        g_followme_active = true;
        g_last_followme_send_tick = xTaskGetTickCount();
        ESP_LOGI(TAG, "Follow-Me heartbeat sent: ambient=%dC mode=%u setpoint=%dC",
                 ambient_temp_c, ir_mode, setpoint_c);
    }
    return err;
}

// Small delay between each Matter attribute update below -- confirmed on
// real hardware (2026-08-31) that firing all of these back-to-back with no
// spacing produces a dense burst of near-simultaneous IM reports, and on
// this device's marginal Thread link, a burst that size was enough to
// trigger a genuine "No available message buffer" cascade (the exact same
// packets retransmitting repeatedly, several times a second, until
// exhaustion -- CHIP's own reliable-messaging retries piling up faster than
// the link could ack them). Increasing the buffer pool alone didn't help,
// since the problem is the burst rate, not the total budget. This function
// runs from sync_task (16KB stack, priority 4), so a few hundred ms of
// total added latency here is trivial against its 5-minute poll interval.
#define MATTER_UPDATE_BURST_SPACING_MS 50

// OpenThread's own sentinel for "no RSSI available" -- matches
// OT_RADIO_RSSI_INVALID (openthread/platform/radio.h) without pulling in
// that platform header just for one constant.
#define THREAD_RSSI_INVALID 127

// Role-aware: confirmed live (2026-09-01) via the border router's own
// `ot-ctl router table`/`ot-ctl neighbor table` that this device operates
// as a Thread ROUTER, not a Child -- expected, since CONFIG_OPENTHREAD_FTD=y
// makes it router-eligible, and the network apparently promoted it. Routers
// have no "parent," only peer neighbors, so otThreadGetParentAverageRssi()
// was silently failing (and returning its own invalid sentinel) every
// single time it was called, all night, regardless of how good the actual
// link was -- confirmed separately that the real link was a healthy
// -71 dBm the whole time. Caller must already hold the OpenThread lock (or
// be running from OpenThread's own task context, e.g. a state-changed
// callback) and pass a non-null instance.
static int8_t read_thread_link_rssi_locked(otInstance *instance)
{
    int8_t rssi = THREAD_RSSI_INVALID;
    otDeviceRole role = otThreadGetDeviceRole(instance);
    if (role == OT_DEVICE_ROLE_CHILD) {
        otThreadGetParentAverageRssi(instance, &rssi);
        return rssi;
    }
    if (role != OT_DEVICE_ROLE_ROUTER && role != OT_DEVICE_ROLE_LEADER) {
        // Detached/disabled -- no parent and no meaningful neighbor table
        // either; the invalid sentinel is the honest answer here.
        return rssi;
    }
    // Average across router-peer neighbors, excluding any children of our
    // own (CONFIG_OPENTHREAD_MLE_MAX_CHILDREN allows this device to have
    // some, though unlikely in practice) -- what matters is the link
    // toward the rest of the mesh/border router, not to something attached
    // to us.
    otNeighborInfoIterator iter = OT_NEIGHBOR_INFO_ITERATOR_INIT;
    otNeighborInfo info;
    int32_t sum = 0;
    int count = 0;
    while (otThreadGetNextNeighborInfo(instance, &iter, &info) == OT_ERROR_NONE) {
        if (info.mIsChild) {
            continue;
        }
        sum += info.mAverageRssi;
        count++;
    }
    if (count > 0) {
        rssi = (int8_t)(sum / count);
    }
    return rssi;
}

static int8_t get_thread_link_rssi(void)
{
    int8_t rssi = THREAD_RSSI_INVALID;
    if (!esp_openthread_lock_acquire(pdMS_TO_TICKS(1000))) {
        return rssi;
    }
    otInstance *instance = esp_openthread_get_instance();
    if (instance) {
        rssi = read_thread_link_rssi_locked(instance);
    }
    esp_openthread_lock_release();
    return rssi;
}

static void apply_status_to_matter(const tuya_device_status_t *device_status)
{
    matter_update_onoff(device_status->switch_state);
    vTaskDelay(pdMS_TO_TICKS(MATTER_UPDATE_BURST_SPACING_MS));
    // Mini-split's own indoor reading -- the Thermostat's LocalTemperature.
    matter_update_local_temperature(device_status->temp_current);
    vTaskDelay(pdMS_TO_TICKS(MATTER_UPDATE_BURST_SPACING_MS));
    // Derived from temp_set_f (Fahrenheit-native), not the raw temp_set
    // Celsius DP -- added 2026-09-01 after finding they can genuinely
    // disagree (temp_set_f is Tuya's/the unit's own trusted field per
    // tuya_setpoint_c_to_f()'s doc comment; temp_set is coarser 0.5C-quantized
    // and was found sitting a full degree off it one night). This keeps
    // thermostat1's mirrored setpoint consistent with what sync_task's
    // setpoint-mismatch reconciliation above already treats as ground truth.
    int16_t confirmed_setpoint_c = tuya_setpoint_f_to_c(device_status->temp_set_f);
    matter_update_heating_setpoint(confirmed_setpoint_c);
    vTaskDelay(pdMS_TO_TICKS(MATTER_UPDATE_BURST_SPACING_MS));
    matter_update_cooling_setpoint(confirmed_setpoint_c);
    vTaskDelay(pdMS_TO_TICKS(MATTER_UPDATE_BURST_SPACING_MS));
    matter_update_system_mode(map_tuya_mode_to_matter(device_status));
    vTaskDelay(pdMS_TO_TICKS(MATTER_UPDATE_BURST_SPACING_MS));
    // effective_fresh_air(), not the raw reported value, so the switch
    // doesn't snap back while a fresh command is still waiting on Tuya.
    matter_update_fresh_air(effective_fresh_air(device_status));
    vTaskDelay(pdMS_TO_TICKS(MATTER_UPDATE_BURST_SPACING_MS));

    uint8_t compressor_pct = compressor_demand_percent(device_status);
    matter_update_compressor_demand(compressor_pct);
    vTaskDelay(pdMS_TO_TICKS(MATTER_UPDATE_BURST_SPACING_MS));
    matter_update_compressor_running(compressor_pct > 0);
    vTaskDelay(pdMS_TO_TICKS(MATTER_UPDATE_BURST_SPACING_MS));

    // Mini-split's own outdoor ambient reading -- separate endpoint from both
    // indoor temperatures above.
    matter_update_outdoor_temperature(device_status->outdoor_temp);
    vTaskDelay(pdMS_TO_TICKS(MATTER_UPDATE_BURST_SPACING_MS));

    // Outage active/reason -- see outage_log.h. Reflects whatever's been
    // recorded as of this poll; the detectors themselves (sync_task's retry
    // loop and reconciliation block, the Thread state-change callback, and
    // the boot-time reset-reason check) are what actually decide when an
    // outage starts/ends.
    //
    // 2026-09-07: reason now comes from outage_log_active_reason(), not
    // outage_log_last_reason() -- the latter can reflect an already-closed
    // record (e.g. a brief resolved Tuya-unreachable blip) that was simply
    // logged more recently than a still-open one (e.g. a long-running
    // setpoint mismatch), which made this display disagree with what
    // outage_log_any_active() just reported as still active. See
    // outage_log_active_reason()'s doc comment.
    matter_update_outage_active(outage_log_any_active());
    vTaskDelay(pdMS_TO_TICKS(MATTER_UPDATE_BURST_SPACING_MS));
    matter_update_outage_reason(outage_log_active_reason());
    vTaskDelay(pdMS_TO_TICKS(MATTER_UPDATE_BURST_SPACING_MS));

    // Thread parent RSSI -- piggybacks on this same 5-minute sync_task poll
    // cadence rather than anything tighter, deliberately: RSSI fluctuates
    // constantly, and reporting it more often would be exactly the kind of
    // steady-state Matter chatter that caused tonight's NoBufs cascades. No
    // separate local log for this (deliberately, per the user) -- RSSI is
    // only recorded locally as a snapshot on an actual outage, via
    // outage_log_start()/outage_log_record_point_event()'s rssi_dbm
    // parameter, not as its own continuous time series.
    matter_update_thread_rssi(get_thread_link_rssi());
}

// Opens/closes OUTAGE_REASON_SETPOINT_MISMATCH based on whether the
// Desired Setpoint (Matter, HA-writable) currently agrees with Tuya's
// reported temp_set_f within 1F (sync_task's correction itself has been
// exact-match since 2026-10-05; see there for why this stays looser). Shared by every call site that receives a
// fresh Tuya status via cache_and_apply_status() below -- added 2026-09-10,
// same reasoning as note_tuya_poll_success(): before this, only sync_task's
// own 5-minute reconciliation loop ever cleared this outage, so a real
// command that fixed the mismatch via command_task/post_send_verify_and_sync
// (which can confirm within ~15-90s) still left the outage showing "active"
// in HA for up to a full STATUS_POLL_INTERVAL_MS until sync_task's own next
// poll happened to notice. Bookkeeping only -- does NOT send an IR
// correction; that stays exclusively in sync_task's own loop (see there),
// so this can safely run from command_task's pre-send refreshes too without
// triggering a redundant extra send.
static void check_setpoint_mismatch_outage(const tuya_device_status_t *status)
{
    int16_t desired_c_x100 = matter_get_desired_cooling_setpoint();
    int16_t desired_f = tuya_setpoint_c_to_f(desired_c_x100);
    int16_t mismatch_f = (int16_t)abs(desired_f - status->temp_set_f);
    if (mismatch_f > 1) {
        outage_log_start(OUTAGE_REASON_SETPOINT_MISMATCH, get_thread_link_rssi());
    } else {
        outage_log_end(OUTAGE_REASON_SETPOINT_MISMATCH);
    }
}

static void cache_and_apply_status(const tuya_device_status_t *device_status)
{
    taskENTER_CRITICAL(&g_status_cache_lock);
    g_last_device_status = *device_status;
    g_last_device_status_valid = true;
    taskEXIT_CRITICAL(&g_status_cache_lock);
    // Must run before apply_status_to_matter() -- that's what pushes
    // outage_log_any_active()/outage_log_active_reason() to HA, so the
    // bookkeeping above needs to be current before that push happens (same
    // ordering lesson as note_tuya_poll_success()'s call sites).
    check_setpoint_mismatch_outage(device_status);
    note_actual_mode(map_tuya_mode_to_matter(device_status));
    apply_status_to_matter(device_status);
}

static void reconcile_system_mode(const tuya_device_status_t *status)
{
    TickType_t now = xTaskGetTickCount();
    mode_reconcile_input_t in = {
        .actual = map_tuya_mode_to_matter(status),
        .desired = matter_get_desired_system_mode(),
        .command_pending = matter_get_system_mode_command() != 0xFF || matter_get_onoff_command(),
        .unit_moved_last = (now - g_actual_mode_changed_tick) < (now - g_desired_mode_changed_tick),
    };
    in.actual_recently_commanded = mode_commanded_within(in.actual, MODE_ECHO_WINDOW_MS);
    in.desired_recently_commanded = mode_commanded_within(in.desired, MODE_RESEND_HOLDOFF_MS);

    switch (mode_reconcile_decide(&in, &g_mode_mismatch_polls)) {
        case MODE_RECONCILE_IN_SYNC:
            return;
        case MODE_RECONCILE_ADOPT_UNKNOWN:
            ESP_LOGI(TAG, "Mode reconcile: no desired mode recorded yet, adopting unit's mode %u", in.actual);
            break;
        case MODE_RECONCILE_WAIT_POLLS:
            ESP_LOGW(TAG, "Mode reconcile: unit %u vs desired %u (poll %u/%u), waiting",
                     in.actual, in.desired, g_mode_mismatch_polls, MODE_MISMATCH_POLLS);
            return;
        case MODE_RECONCILE_ADOPT:
            ESP_LOGW(TAG, "Mode reconcile: unit changed to %u on its own (remote/app), Desired adopts it (was %u)",
                     in.actual, in.desired);
            break;
        case MODE_RECONCILE_ADOPT_UNMAPPABLE:
            ESP_LOGW(TAG, "Mode reconcile: desired mode %u has no IR equivalent, adopting unit's %u",
                     in.desired, in.actual);
            break;
        case MODE_RECONCILE_WAIT_ECHO:
            ESP_LOGW(TAG, "Mode reconcile: unit %u vs desired %u, waiting for the recent send to show up in Tuya",
                     in.actual, in.desired);
            return;
        case MODE_RECONCILE_RESEND: {
            ESP_LOGW(TAG, "Mode reconcile: unit %u vs desired %u -- resending desired mode via IR", in.actual, in.desired);
            esp_err_t err = (in.desired == 0)
                ? send_ir_frame(status, false, false, 0, false, 0)
                : send_ir_frame(status, true, true, (uint8_t)map_matter_mode_to_ir(in.desired),
                                true, matter_get_desired_cooling_setpoint());
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Mode reconcile: IR send failed: %s", esp_err_to_name(err));
                return;
            }
            record_mode_command(in.desired);
            g_mode_mismatch_polls = 0;
            return;
        }
    }
    // The three ADOPT outcomes.
    matter_set_desired_system_mode(in.actual);
    note_desired_mode_changed();
}

/**
 * @brief Wait until system time is synchronized via SNTP
 *
 * Retries indefinitely rather than giving up after a fixed number of
 * attempts. This used to ESP_ERROR_CHECK-abort (full reboot) after 20
 * one-second retries, which -- on Thread, where NTP depends on border
 * routing/DNS64 being fully up -- destroyed freshly-completed Matter/Thread
 * commissioning state on every transient hiccup. A slow or temporarily
 * unreachable NTP server should not cost the device its fabric.
 */
static esp_err_t wait_for_time_sync(void)
{
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;

    ESP_LOGI(TAG, "Starting SNTP time sync...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    while (1) {
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year >= (2024 - 1900)) {
            // Pacific time, DST-aware (PST8PDT with US DST rules) -- matches
            // the rest of the home automation stack (HA, wyse). Nothing
            // before this point should call localtime_r()/asctime() and
            // expect local time; everything after can. No effect on time(),
            // only on the libc calls that consult TZ.
            setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
            tzset();
            localtime_r(&now, &timeinfo);
            ESP_LOGI(TAG, "Time synchronized: %s", asctime(&timeinfo));
            return ESP_OK;
        }
        if (retry > 0 && retry % 20 == 0) {
            ESP_LOGW(TAG, "Still waiting for SNTP time sync (%d s)...", retry);
        }
        retry++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ============================================================================
// Phase 3: Bidirectional Integration
// ============================================================================

/**
 * @brief Synchronization task: polls Tuya and updates Matter
 * 
 * Flow:
 * 1. Get current status from Tuya device
 * 2. Update Matter attributes with Tuya data
 * 3. Handle errors and retries
 * 4. Log status for debugging
 */
// sync_task's view of the unit at its previous poll, for
// setpoint_reconcile_decide(). Only sync_task touches these.
static int16_t s_prev_unit_setpoint_f = SETPOINT_UNKNOWN_F;
static uint8_t s_prev_unit_mode = MATTER_MODE_UNKNOWN;
static int16_t s_pending_manual_setpoint_f = SETPOINT_UNKNOWN_F;

static void sync_task(void *param)
{
    ESP_LOGI(TAG, "Status synchronization task started (interval: %ums)", STATUS_POLL_INTERVAL_MS);

    // Initial delay to let device stabilize
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Fetch and publish real Tuya status immediately on the first pass
    // (skipping the poll-interval wait) so the Matter node doesn't sit on
    // its hardcoded boot-time defaults for a full poll interval, which used
    // to look like the mini-split turning itself off on every reboot even
    // though the real unit was never touched (the on/off default is now
    // seeded to true instead specifically to make that cosmetic gap
    // harmless either way -- see g_matter_state's onoff initializer). This
    // must stay in sync_task rather than app_main() -- tuya_get_device_status()
    // does HTTPS/TLS + cJSON parsing and needs the 16KB stack this task is
    // given below, not app_main's default ~3.5KB main task stack (which it
    // blew through, crash-looping the device on every boot when tried
    // inline in app_main()).
    bool first_pass = true;

    while (1) {
        if (!first_pass) {
            vTaskDelay(pdMS_TO_TICKS(STATUS_POLL_INTERVAL_MS));
        }
        first_pass = false;

        tuya_device_status_t device_status = {0};

        // Attempt to get device status with retries, backing off
        // exponentially (2s, 4s, ...) so a failure storm doesn't multiply
        // Tuya API call volume at a fixed high rate.
        esp_err_t result = ESP_FAIL;
        for (uint8_t attempt = 0; attempt < MAX_RETRIES; attempt++) {
            result = tuya_get_device_status(&device_status);

            if (result == ESP_OK) {
                note_tuya_poll_success();
                break;
            }

            // Retry with exponential backoff
            if (attempt < MAX_RETRIES - 1) {
                uint32_t backoff_delay_ms = RETRY_DELAY_MS << attempt;
                ESP_LOGW(TAG, "Status poll attempt %u failed, retrying in %ums...",
                         attempt + 1, backoff_delay_ms);
                vTaskDelay(pdMS_TO_TICKS(backoff_delay_ms));
            }
        }
        
        if (result != ESP_OK) {
            if (g_sync_state.status_poll_failures == 0) {
                // First failure after a prior success -- outage begins.
                outage_log_start(OUTAGE_REASON_TUYA_UNREACHABLE, get_thread_link_rssi());
            }
            g_sync_state.status_poll_failures++;
            ESP_LOGE(TAG, "Failed to get Tuya status (failures: %u)",
                     g_sync_state.status_poll_failures);

            // After multiple failures, consider device offline
            if (g_sync_state.status_poll_failures > 5) {
                ESP_LOGW(TAG, "Multiple status poll failures - device may be offline");
            }
            continue;
        }
        
        // Log current status
        ESP_LOGI(TAG, "Tuya Status Update:");
        ESP_LOGI(TAG, "  Power: %s", device_status.switch_state ? "ON" : "OFF");
        ESP_LOGI(TAG, "  Current Temp: %.1f°C", device_status.temp_current / 100.0f);
        ESP_LOGI(TAG, "  Set Temp: %.1f°C", device_status.temp_set / 100.0f);
        ESP_LOGI(TAG, "  Mode: %s%s", ac_mode_name(device_status.ac_mode),
                 device_status.heat ? " (Aux Heat ON)" : "");
        ESP_LOGI(TAG, "  Compressor: %d%% (%dHz)  Outdoor Temp: %.1f°C",
                 compressor_demand_percent(&device_status),
                 device_status.compressor_frequency,
                 device_status.outdoor_temp / 100.0f);

        // No reconciliation against a locally-expected value anymore -- just
        // apply whatever Tuya's shadow actually says. If a command we sent
        // failed or hasn't landed yet, this plainly shows that, and the next
        // poll (every STATUS_POLL_INTERVAL_MS, fixed) will show whatever's
        // true then.
        cache_and_apply_status(&device_status);

        g_sync_state.last_status_update = xTaskGetTickCount();

        // Desired-setpoint mismatch correction: the standalone Desired
        // Setpoint Matter endpoint (matter_get_desired_cooling_setpoint(),
        // HA-writable, never touched by this task) is compared against what
        // Tuya just reported, in whole-Fahrenheit-degree terms -- comparing
        // raw Celsius is unreliable here since Tuya's temp_set only stores
        // 0.5C steps, so a whole-Fahrenheit command doesn't generally
        // round-trip back to an exact Celsius match even once genuinely
        // applied (see tuya_setpoint_c_to_f()'s doc comment).
        //
        // OUTAGE_REASON_SETPOINT_MISMATCH's open/close bookkeeping itself
        // now lives in check_setpoint_mismatch_outage() (called from
        // cache_and_apply_status() above, same >1F tolerance) -- moved
        // there 2026-09-10 so every call site that gets a fresh Tuya status
        // clears it, not just this once-per-5-minutes loop (see that
        // function's doc comment). This block only decides whether to
        // actually *send an IR correction*, which stays exclusive to this
        // task -- command_task's own pre-send refreshes must NOT also
        // trigger a correction send here.
        //
        // Re-added 2026-09-09: sends a correction on a genuine mismatch,
        // gated by a >1F tolerance rather than exact equality. An earlier
        // version corrected on any exact mismatch and was removed
        // 2026-09-07 because it fired a real IR command on every single
        // boot: a persisted NVS "desired" value essentially never matches
        // Tuya's independently-derived temp_set_f by exact coincidence
        // (their C->F rounding conventions can legitimately disagree by a
        // degree even when the AC is genuinely at the requested
        // temperature), so exact-match "correction" would have kept firing
        // every 5-minute poll indefinitely, not just once per boot. The >1F
        // tolerance absorbs that class of rounding-convention noise while
        // still catching and correcting a real, larger divergence (e.g. a
        // command that silently failed to land, or an out-of-band change
        // back toward a stale setpoint).
        //
        // 2026-10-04: skipped while the unit is off. This frame always sets
        // Power On, and with the unit off its mode byte falls back to Auto
        // (kOff has no IR mode) -- so a correction sent while off would turn
        // the unit back on in Auto. Mode reconciliation below owns power.
        //
        // 2026-10-05: also skipped while the unit is in Auto (Tuya mode 0) --
        // it ignores the setpoint byte in Auto (IR_PROTOCOL_REFERENCE.md),
        // so this resent IR every 5 min to no effect (seen live: remote set
        // Auto/80F, Desired 69F stayed unapplied). Leaving Auto via a mode
        // command now carries Desired's setpoint (see command_task).
        //
        // 2026-10-05: exact match now, no >1F tolerance. The "rounding-
        // convention noise" above was really the half-degree bug (state[12]
        // 0x04 left set on every frame, so every whole-degree setpoint landed
        // 1F high -- see build_ir_state_frame()). With that fixed every whole
        // F in 61-86 lands exactly, and build_ir_state_frame() snaps Desired to
        // the whole F compared here, so any difference is a real miss or a
        // remote setpoint change (which HA's Desired now overrides). The
        // SETPOINT_MISMATCH outage keeps its >1F tolerance -- it also makes
        // HA's setpoint automation hold, which a 1F in-flight gap shouldn't.
        //
        // 2026-10-05: and a mismatch is no longer always corrected -- a
        // setpoint changed on the remote/app is adopted into Desired instead
        // (setpoint_reconcile_decide(): the unit moved to a value the bridge
        // didn't send, outside a mode change, held for two polls). A missed
        // update -- the unit still at its previous value -- is resent as
        // before. HA's setpoint automation treats an adopted value as a
        // manual override until the next day/night change.
        int16_t desired_c_x100 = matter_get_desired_cooling_setpoint();
        int16_t last_sent_f;
        uint32_t ms_since_sent;
        bool sent_any = setpoint_last_sent(&last_sent_f, &ms_since_sent);
        uint8_t unit_mode_now = map_tuya_mode_to_matter(&device_status);
        setpoint_reconcile_input_t sp_in = {
            .desired_f = tuya_setpoint_c_to_f(desired_c_x100),
            .unit_f = device_status.temp_set_f,
            .prev_unit_f = s_prev_unit_setpoint_f,
            .correctable = device_status.switch_state && device_status.ac_mode != 0,
            .mode_changed = s_prev_unit_mode != MATTER_MODE_UNKNOWN && unit_mode_now != s_prev_unit_mode,
            .sent_recently = sent_any && ms_since_sent < SETPOINT_ECHO_WINDOW_MS,
            .last_sent_f = last_sent_f,
        };
        s_prev_unit_setpoint_f = device_status.temp_set_f;
        s_prev_unit_mode = unit_mode_now;
        switch (setpoint_reconcile_decide(&sp_in, &s_pending_manual_setpoint_f)) {
            case SETPOINT_RECONCILE_WAIT:
                ESP_LOGW(TAG, "Setpoint: unit moved to %dF on its own (desired %dF) -- confirming on next poll before adopting",
                         sp_in.unit_f, sp_in.desired_f);
                break;
            case SETPOINT_RECONCILE_ADOPT:
                ESP_LOGW(TAG, "Setpoint: unit held %dF (remote/app change), Desired adopts it (was %dF)",
                         sp_in.unit_f, sp_in.desired_f);
                matter_set_desired_setpoint(tuya_setpoint_f_to_c(sp_in.unit_f));
                check_setpoint_mismatch_outage(&device_status);
                break;
            case SETPOINT_RECONCILE_RESEND: {
                ESP_LOGW(TAG, "Setpoint mismatch (desired %dF, Tuya reports %dF) -- sending IR correction",
                         sp_in.desired_f, sp_in.unit_f);
                esp_err_t send_err = send_ir_frame(&device_status, true, false, 0, true, desired_c_x100);
                if (send_err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send setpoint correction via IR: %s", esp_err_to_name(send_err));
                }
                break;
            }
            case SETPOINT_RECONCILE_SKIP:
            case SETPOINT_RECONCILE_IN_SYNC:
                break;
        }

        reconcile_system_mode(&device_status);
    }
}

// Post-send verification/retry-poll (PLAN.md Milestone 4's "post-send
// retry-poll loop", not previously implemented). Real hardware needs time
// to actually apply an IR-driven command and report it back up through its
// own Tuya/WiFi module -- confirmed via live HA testing 2026-09-07: a
// pre-send refresh (right before sending) or an immediate post-send GET
// both still see the OLD value; only sync_task's next full
// STATUS_POLL_INTERVAL_MS (5 minute) poll happened to be slow enough for
// that round-trip to have finished by the time it asked again. This closes
// that gap to roughly a minute instead of up to five, by re-checking a few
// times and updating thermostat1 (EP1)'s Matter mirror via
// cache_and_apply_status() as soon as Tuya's own reported state actually
// reflects the change (or after giving up).
//
// Runs inline/blocking in command_task, same as the pre-send refresh
// already does -- a command arriving during this window isn't lost, just
// picked up on the next loop iteration once this one returns.
#define POST_SEND_VERIFY_RETRY_DELAY_MS 15000
#define POST_SEND_VERIFY_MAX_ATTEMPTS 6  // ~90s total

static void post_send_verify_and_sync(bool check_power, bool expected_power_on,
                                        bool check_setpoint, int16_t expected_setpoint_c_x100,
                                        bool check_mode, uint8_t expected_ac_mode)
{
    for (int attempt = 1; attempt <= POST_SEND_VERIFY_MAX_ATTEMPTS; attempt++) {
        vTaskDelay(pdMS_TO_TICKS(POST_SEND_VERIFY_RETRY_DELAY_MS));

        tuya_device_status_t status;
        status_cache_get(&status);
        if (tuya_get_device_status(&status) != ESP_OK) {
            ESP_LOGW(TAG, "Post-send verify poll %d/%d: Tuya GET failed, retrying",
                     attempt, POST_SEND_VERIFY_MAX_ATTEMPTS);
            continue;
        }

        note_tuya_poll_success();
        cache_and_apply_status(&status);
        g_sync_state.last_status_update = xTaskGetTickCount();

        bool power_ok = !check_power || (status.switch_state == expected_power_on);
        // Within half a degree -- temp_set_f-derived Celsius can legitimately
        // disagree with the raw target by quantization, same tolerance
        // reasoning as apply_status_to_matter()'s own comment.
        bool setpoint_ok = !check_setpoint ||
            (abs(tuya_setpoint_f_to_c(status.temp_set_f) - expected_setpoint_c_x100) <= 50);
        bool mode_ok = !check_mode || (status.ac_mode == expected_ac_mode);

        if (power_ok && setpoint_ok && mode_ok) {
            ESP_LOGI(TAG, "Post-send verify: Tuya confirms the change after %d attempt(s)", attempt);
            return;
        }
    }
    ESP_LOGW(TAG, "Post-send verify: gave up after %d attempts, thermostat1 may still be stale "
                  "until sync_task's next poll", POST_SEND_VERIFY_MAX_ATTEMPTS);
}

// Follow-Me heartbeat interval -- matches the real remote's observed 3-minute
// re-send cadence (see IR_PROTOCOL_REFERENCE.md's "Follow Me behavior"
// section), not derived from the unverified ~10-minute fallback-timeout
// guess mentioned there.
#define FOLLOWME_HEARTBEAT_INTERVAL_MS (3 * 60 * 1000)

/**
 * @brief Follow-Me task (PLAN.md Milestone 3): periodically sends the
 *        ambient-temperature sensor reading to the unit over IR.
 *
 * The reading itself comes from HA (matter_get_followme_ambient_temp_c_x100()),
 * which relays the real Zigbee2MQTT sensor value via an automation writing
 * to the Follow-Me Matter endpoint -- this firmware has no MQTT client of
 * its own (see that getter's doc comment for why). Always active whenever a
 * value has been set and a confirmed Tuya state are both available -- no
 * separate HA-exposed enable/disable control, matching this project's
 * minimal-surface approach elsewhere.
 *
 * Every send is a silent heartbeat (2026-10-05 -- see send_followme_frame()'s
 * comment for why the beeping enable instance was dropped).
 *
 * The very first loop iteration after boot skips the interval wait below
 * (see first_pass) -- fixed 2026-09-10 after finding this task always slept
 * a full FOLLOWME_HEARTBEAT_INTERVAL_MS (3 minutes) before its very first
 * check on every boot. The data-validity checks just below (ambient reading
 * available, confirmed Tuya state available) already guard against acting
 * on stale/default state, and both are normally available within seconds of
 * boot.
 *
 * Schedules strictly off its own last send (g_last_followme_send_tick), not
 * off any command send -- see that variable's doc comment (2026-10-01): a
 * command arriving between heartbeats no longer pushes the next one back.
 */
static void followme_task(void *param)
{
    ESP_LOGI(TAG, "Follow-Me task started (interval: %ums)", FOLLOWME_HEARTBEAT_INTERVAL_MS);

    bool first_pass = true;

    while (1) {
        // Re-derive the remaining wait from g_last_followme_send_tick every
        // time we wake, rather than a single fixed vTaskDelay -- this is what
        // keeps the heartbeat on a strict 3-minute cadence even across a
        // skipped tick (see the skip branches below, which don't advance this
        // tick, so a retry still targets the original schedule, not a fresh
        // 3 minutes from the retry). Skipped on the very first iteration
        // (first_pass) -- see this function's doc comment.
        TickType_t interval_ticks = pdMS_TO_TICKS(FOLLOWME_HEARTBEAT_INTERVAL_MS);
        TickType_t elapsed = xTaskGetTickCount() - g_last_followme_send_tick;
        if (!first_pass && elapsed < interval_ticks) {
            vTaskDelay(interval_ticks - elapsed);
            continue;
        }
        first_pass = false;

        int8_t ambient_c;
        if (!followme_ambient_whole_c(&ambient_c)) {
            ESP_LOGW(TAG, "Follow-Me: no ambient sensor reading from HA yet, skipping this tick");
            g_followme_active = false;
            vTaskDelay(interval_ticks);
            continue;
        }
        tuya_device_status_t cached_status;
        if (!status_cache_get(&cached_status)) {
            ESP_LOGW(TAG, "Follow-Me: no confirmed Tuya state yet, skipping this tick");
            vTaskDelay(interval_ticks);
            continue;
        }
        // 2026-10-04: every Follow-Me frame sets Power On, and with the unit
        // off its mode byte falls back to Auto (kOff has no IR mode) -- so
        // this heartbeat was turning an off unit back on in Auto. Matches
        // the unit going off->auto with no command from HA on 2026-10-01
        // and 2026-10-03. Skip while off; the first heartbeat after it turns
        // back on re-engages Follow-Me.
        if (!cached_status.switch_state) {
            g_followme_active = false;
            vTaskDelay(interval_ticks);
            continue;
        }

        esp_err_t err = send_followme_frame(&cached_status, ambient_c);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Follow-Me send failed: %s", esp_err_to_name(err));
        }
    }
}

/**
 * @brief Command routing task: checks for Matter commands and sends to Tuya
 *
 * Flow:
 * 1. Check if the controller sent any commands
 * 2. Route to appropriate Tuya API call
 * 3. Clear command flag after processing
 * 4. Handle errors gracefully
 */
static void command_task(void *param)
{
    ESP_LOGI(TAG, "Command routing task started (interval: %ums)", 
             COMMAND_POLL_INTERVAL_MS);
    
    // Initial delay
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(COMMAND_POLL_INTERVAL_MS));
        
        g_sync_state.last_command_check = xTaskGetTickCount();
        
        // ===== Check for OnOff command from Matter =====
        bool onoff_pending = matter_get_onoff_command();
        if (onoff_pending) {
            bool desired_onoff = matter_get_onoff_state();
            ESP_LOGI(TAG, "Processing OnOff command from controller: %s",
                     desired_onoff ? "ON" : "OFF");

            // Wired to real IR as of 2026-09-07 -- the Power bit (state[5]
            // bit 0x04) is confirmed (see IR_PROTOCOL_REFERENCE.md) and
            // send_ir_frame() now takes it as an explicit parameter instead
            // of always forcing power on. Same pre-send-refresh pattern as
            // the Desired Setpoint/System Mode blocks below, so the mode/
            // setpoint bytes this frame preserves reflect current reality.
            tuya_device_status_t refreshed_status;
            status_cache_get(&refreshed_status);
            if (tuya_get_device_status(&refreshed_status) == ESP_OK) {
                note_tuya_poll_success();
                cache_and_apply_status(&refreshed_status);
                g_sync_state.last_status_update = xTaskGetTickCount();
            } else {
                ESP_LOGW(TAG, "Pre-send Tuya refresh failed; using last known state for the IR frame's mode/setpoint bytes");
            }

            // Dedup against the freshly-refreshed shadow state (PLAN.md
            // Milestone 2): if Tuya already reports the desired power state
            // -- e.g. the physical remote or Tuya app already made this
            // exact change -- skip the redundant IR send and its ~90s
            // post-send verify loop entirely, rather than re-transmitting a
            // command that wouldn't change anything.
            // 2026-10-04: keep the Desired Setpoint endpoint's mode in step
            // with a power change from an OnOff endpoint, or mode
            // reconciliation would undo it -- Off sets desired mode Off; On
            // from Off clears it so the next poll adopts whatever mode the
            // unit comes back on in.
            if (!desired_onoff) {
                matter_set_desired_system_mode(0);
                note_desired_mode_changed();
            } else if (matter_get_desired_system_mode() == 0) {
                matter_set_desired_system_mode(MATTER_DESIRED_MODE_UNKNOWN);
                note_desired_mode_changed();
            }

            if (refreshed_status.switch_state == desired_onoff) {
                ESP_LOGI(TAG, "Power already %s per fresh Tuya status, skipping redundant IR send",
                         desired_onoff ? "on" : "off");
                matter_clear_onoff_command();
            } else {
                esp_err_t result = send_ir_frame(&refreshed_status, desired_onoff, false, 0, false, 0);
                if (result != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send power command via IR: %s", esp_err_to_name(result));
                } else if (!desired_onoff) {
                    record_mode_command(0);
                } else {
                    tuya_device_status_t on_status = refreshed_status;
                    on_status.switch_state = true;
                    record_mode_command(map_tuya_mode_to_matter(&on_status));
                }

                matter_clear_onoff_command();

                post_send_verify_and_sync(true, desired_onoff, false, 0, false, 0);
            }
        }

        // ===== Check for a Desired Setpoint change from Matter =====
        // Added 2026-08-30: sync_task's own reconciliation (below) still
        // runs regardless and is what actually keeps things correct if this
        // ever fails or gets missed -- this block only exists so the common,
        // successful case doesn't have to wait for sync_task's next (up to
        // 5-minute) status poll. Deliberately handled here rather than
        // inline in the Matter attribute callback: that callback runs in the
        // Matter/CHIP stack's own context, and the pre-send Tuya status
        // refresh below is a blocking HTTPS call (now bounded at TUYA_HTTP_TIMEOUT_MS, but still
        // multiple seconds in the normal case) -- blocking that callback
        // directly risks stalling Matter's own event processing. Routing
        // through command_task's existing 5-second poll keeps every real
        // Tuya API call serialized through the one task that already owns
        // g_tuya_ctx's shared state (access token, etc.), avoiding any
        // concurrent-access race with sync_task's own calls.
        if (matter_get_desired_setpoint_command_pending()) {
            int16_t desired_c_x100 = matter_get_desired_cooling_setpoint();
            ESP_LOGI(TAG, "Desired setpoint changed via Matter, sending via IR");

            // Pre-send refresh (PLAN.md Milestone 2): fetch Tuya's latest
            // status right before building the IR frame, so the mode byte
            // this frame preserves reflects any out-of-band change (real
            // remote, Tuya app) instead of a value that could be stale by
            // up to STATUS_POLL_INTERVAL_MS. This also keeps EP1's mirrored
            // setpoint/temp (and the Tuya app view) fresh within seconds,
            // same benefit the old post-send refresh below used to provide.
            tuya_device_status_t refreshed_status;
            status_cache_get(&refreshed_status);
            if (tuya_get_device_status(&refreshed_status) == ESP_OK) {
                note_tuya_poll_success();
                cache_and_apply_status(&refreshed_status);
                g_sync_state.last_status_update = xTaskGetTickCount();
            } else {
                ESP_LOGW(TAG, "Pre-send Tuya refresh failed; using last known state for the IR frame's mode byte");
            }

            // Dedup against the freshly-refreshed shadow state (PLAN.md
            // Milestone 2) -- same half-degree tolerance
            // post_send_verify_and_sync() uses, since temp_set_f-derived
            // Celsius can legitimately disagree with the raw target by
            // quantization. Skips a redundant IR send (and its ~90s
            // post-send verify loop) when Tuya already reports this exact
            // setpoint, e.g. HA re-sending the same value or the physical
            // remote already having made this change.
            if (abs(tuya_setpoint_f_to_c(refreshed_status.temp_set_f) - desired_c_x100) <= 50) {
                ESP_LOGI(TAG, "Setpoint already matches per fresh Tuya status, skipping redundant IR send");
                matter_clear_desired_setpoint_command_pending();
            } else if (!refreshed_status.switch_state) {
                // 2026-10-04: this frame sets Power On (mode byte falling back
                // to Auto while off), so sending it would turn an off unit on.
                // The new setpoint stays stored; sync_task applies it once the
                // unit is back on.
                ESP_LOGI(TAG, "Unit is off, holding desired setpoint until it's back on");
                matter_clear_desired_setpoint_command_pending();
            } else {
                esp_err_t send_err = send_ir_frame(&refreshed_status, true, false, 0, true, desired_c_x100);
                if (send_err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send desired setpoint via IR: %s", esp_err_to_name(send_err));
                }

                matter_clear_desired_setpoint_command_pending();

                post_send_verify_and_sync(false, false, true, desired_c_x100, false, 0);
            }
        }

        // ===== Check for System Mode command =====
        uint8_t mode_cmd = matter_get_system_mode_command();
        if (mode_cmd != 0xFF) {  // 0xFF = no command
            ESP_LOGI(TAG, "Processing mode command from controller: %u", mode_cmd);
            note_desired_mode_changed();

            // Pre-send refresh (PLAN.md Milestone 2), same reasoning as the
            // Desired Setpoint/OnOff blocks above: fetch Tuya's latest status
            // right before building the IR frame, so the setpoint byte this
            // frame preserves reflects any out-of-band change instead of a
            // possibly-stale cached value.
            tuya_device_status_t refreshed_status;
            status_cache_get(&refreshed_status);
            if (tuya_get_device_status(&refreshed_status) == ESP_OK) {
                note_tuya_poll_success();
                cache_and_apply_status(&refreshed_status);
                g_sync_state.last_status_update = xTaskGetTickCount();
            } else {
                ESP_LOGW(TAG, "Pre-send Tuya refresh failed; using last known state for the IR frame's setpoint byte");
            }

            if (mode_cmd == 0) {
                // kOff -- 2026-10-01: sends a real power-off via IR now,
                // same path the "true power" OnOff endpoint already used.
                // Previously idled in Fan mode instead (see git history),
                // kept running rather than actually powering down; changed
                // on request -- selecting Off should turn the unit off for
                // real. map_tuya_mode_to_matter() already reports kOff
                // whenever switch_state is false (its own first check,
                // above), so no separate proxy-tracking flag is needed to
                // report this back to HA correctly anymore -- removed
                // alongside this (see g_mode_off_via_fan_proxy's old doc
                // comment, now gone).
                if (!refreshed_status.switch_state) {
                    ESP_LOGI(TAG, "Power already off per fresh Tuya status, skipping redundant IR send");
                    matter_clear_mode_command();
                } else {
                    esp_err_t result = send_ir_frame(&refreshed_status, false, false, 0, false, 0);
                    if (result != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to send power-off via IR: %s", esp_err_to_name(result));
                    } else {
                        record_mode_command(0);
                    }
                    matter_clear_mode_command();
                    post_send_verify_and_sync(true, false, false, 0, false, 0);
                }
            } else {
                int8_t tuya_mode = map_matter_mode_to_tuya(mode_cmd);
                int8_t mapped_ir_mode = map_matter_mode_to_ir(mode_cmd);
                if (tuya_mode < 0 || mapped_ir_mode < 0) {
                    ESP_LOGW(TAG, "Matter mode %u has no IR/Tuya equivalent, ignoring", mode_cmd);
                    matter_clear_mode_command();
                } else {
                    uint8_t expected_tuya_mode = (uint8_t)tuya_mode;
                    uint8_t ir_mode = (uint8_t)mapped_ir_mode;

                    // Dedup against the freshly-refreshed shadow state (PLAN.md
                    // Milestone 2): if Tuya already reports the mode this
                    // command asks for, skip the redundant IR send and its
                    // ~90s post-send verify loop.
                    // 2026-10-04: also requires the unit to be on -- comparing
                    // ac_mode alone treated "off, last mode heat" as already
                    // in Heat, so selecting Heat on an off unit sent nothing.
                    bool mode_already_matches = refreshed_status.switch_state &&
                                                (refreshed_status.ac_mode == expected_tuya_mode);
                    if (mode_already_matches) {
                        ESP_LOGI(TAG, "Mode already matches per fresh Tuya status, skipping redundant IR send");
                    } else {
                        // Carries Desired's setpoint, not the unit's current
                        // one (2026-10-05): leaving Auto -- where the unit
                        // ignores setpoints -- otherwise kept whatever the
                        // remote last set (80F in testing) until sync_task's
                        // next correction.
                        esp_err_t result = send_ir_frame(&refreshed_status, true, true, ir_mode,
                                                           true, matter_get_desired_cooling_setpoint());
                        if (result != ESP_OK) {
                            ESP_LOGE(TAG, "Failed to send mode command via IR: %s", esp_err_to_name(result));
                        } else {
                            record_mode_command(mode_cmd);
                        }
                    }

                    ESP_LOGI(TAG, "Mode command processed (ir_mode=%u)%s", ir_mode,
                             mode_already_matches ? " [already matched, no IR sent]" : "");

                    matter_clear_mode_command();

                    if (!mode_already_matches) {
                        post_send_verify_and_sync(true, true, false, 0, true, expected_tuya_mode);
                    }
                }
            }
        }

        // ===== Check for Fresh Air command (2026-10-04) =====
        // Sent as a full-state frame that keeps power/mode/setpoint as Tuya
        // last reported them, with only the Fresh Air bit changed (via
        // effective_fresh_air(), which picks up g_fresh_air_cmd_* set here).
        // Power is passed through as-is, so this never turns the unit on or
        // off as a side effect.
        bool fresh_air_desired = false;
        if (matter_get_fresh_air_command(&fresh_air_desired)) {
            ESP_LOGI(TAG, "Processing Fresh Air command: %s", fresh_air_desired ? "ON" : "OFF");

            // Recorded BEFORE the pre-send refresh below: that refresh
            // re-mirrors the switch from effective_fresh_air(), and with the
            // command not yet recorded it snapped the switch back to Tuya's
            // stale value for ~5 min (2026-10-05).
            g_fresh_air_cmd_value = fresh_air_desired;
            g_fresh_air_cmd_tick = xTaskGetTickCount();
            g_fresh_air_cmd_valid = true;

            tuya_device_status_t refreshed_status;
            status_cache_get(&refreshed_status);
            if (tuya_get_device_status(&refreshed_status) == ESP_OK) {
                note_tuya_poll_success();
                cache_and_apply_status(&refreshed_status);
                g_sync_state.last_status_update = xTaskGetTickCount();
            } else {
                ESP_LOGW(TAG, "Pre-send Tuya refresh failed; using last known state for the Fresh Air frame");
            }

            if (refreshed_status.fresh_air_valve == fresh_air_desired) {
                ESP_LOGI(TAG, "Fresh Air already %s per fresh Tuya status, skipping redundant IR send",
                         fresh_air_desired ? "on" : "off");
            } else {
                esp_err_t result = send_ir_frame(&refreshed_status, refreshed_status.switch_state, false, 0, false, 0);
                if (result != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send Fresh Air command via IR: %s", esp_err_to_name(result));
                }
            }
            matter_clear_fresh_air_command();
        }
    }
}

/**
 * @brief Health monitoring task: logs system status periodically
 */
static void health_task(void *param)
{
    ESP_LOGI(TAG, "Health monitoring task started");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));  // Every 60 seconds

        uint32_t last_update_age_ms =
            (xTaskGetTickCount() - g_sync_state.last_status_update) * portTICK_PERIOD_MS;

        ESP_LOGI(TAG, "=== System Health Check ===");
        ESP_LOGI(TAG, "Network Disconnects: %u", g_sync_state.network_disconnects);
        ESP_LOGI(TAG, "Status Poll Failures: %u", g_sync_state.status_poll_failures);
        ESP_LOGI(TAG, "Last Status Update: %ums ago", last_update_age_ms);

        // Get free memory
        ESP_LOGI(TAG, "Free Heap: %u bytes", esp_get_free_heap_size());

        // Thread link quality: not polled here -- an earlier version tried
        // acquiring the OpenThread API lock each cycle to log parent RSSI,
        // but that lock frequently timed out (OpenThread busy with retries
        // on this weak-signal link), producing more "could not acquire
        // lock" noise than useful signal. OpenThread's own per-packet logs
        // already include "rss:" on most received frames and flow through
        // this same ESP_LOG -> Papertrail pipeline regardless, which covers
        // the same diagnostic need without the extra noise.

        // sync_task should always be making progress well within this
        // window (see SYNC_STALL_RESTART_MS above) -- if it hasn't, it's
        // stuck somewhere a plain HTTP timeout couldn't unblock, and no
        // other task can recover it from the outside. A full restart is
        // the same remedy a physical power cycle provides, without needing
        // physical access.
        if (last_update_age_ms > SYNC_STALL_RESTART_MS) {
            ESP_LOGE(TAG, "sync_task appears stalled (%ums since last successful Tuya poll, "
                          "threshold %ums) -- restarting", last_update_age_ms, SYNC_STALL_RESTART_MS);
            esp_restart();
        }

        // Additional diagnostics can be added here
    }
}

// Tracks whether the last-seen role was attached (Child/Router/Leader), so
// the callback below only opens/closes an outage record on an actual
// attached<->detached transition, not on every role change (e.g.
// Child->Router while still attached the whole time shouldn't count).
static bool s_thread_was_attached = false;

static void thread_state_changed_cb(otChangedFlags flags, void *context)
{
    (void)context;
    if (!(flags & OT_CHANGED_THREAD_ROLE)) {
        return;
    }
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        return;
    }
    otDeviceRole role = otThreadGetDeviceRole(instance);
    bool attached = (role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER ||
                      role == OT_DEVICE_ROLE_LEADER);

    if (attached && !s_thread_was_attached) {
        outage_log_end(OUTAGE_REASON_THREAD_DISCONNECTED);
    } else if (!attached && s_thread_was_attached) {
        // read_thread_link_rssi_locked() directly here, not the
        // lock-acquiring get_thread_link_rssi() -- this callback already
        // runs from within OpenThread's own task context (invoked directly
        // by the OT stack), so taking the app-side esp_openthread_lock
        // would be redundant at best and a possible deadlock at worst.
        // otThreadGetDeviceRole() inside it will read whatever role we're
        // transitioning *into* (Detached/Disabled), which correctly falls
        // through to the invalid sentinel -- there's no parent and no
        // meaningful neighbor table left to read from at this exact moment
        // either way, so that's the honest answer.
        outage_log_start(OUTAGE_REASON_THREAD_DISCONNECTED, read_thread_link_rssi_locked(instance));
    }
    s_thread_was_attached = attached;
}

/**
 * @brief Application main entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "\n\n=== MiniSplit Matter Bridge %s Starting ===\n", esp_app_get_description()->version);
    
    // Initialize NVS flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    g_app_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(g_app_event_group ? ESP_OK : ESP_FAIL);

    // Initialize TCP/IP stack before Matter brings up its own (Thread) netif.
    ESP_ERROR_CHECK(esp_netif_init());

    // Initialize event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Papertrail temporarily disabled (2026-08-31) to test whether its UDP
    // traffic -- even after filtering out OpenThread's own routine trace
    // logs -- is still adding enough load to this congested Thread link to
    // worsen CASE re-establishment during NoBufs storms. Every remaining
    // MAIN/TUYA_CLIENT log line still sends a UDP packet over the same link
    // that's struggling; disabling entirely removes that variable so we can
    // see whether disconnect frequency/duration actually improves without
    // it. Re-enable by uncommenting once this comparison is done.
    // papertrail_logger_init(PAPERTRAIL_HOST, PAPERTRAIL_PORT, PAPERTRAIL_SYSTEMNAME);

    // Local, network-free replacement: a flash-backed ring log, off by
    // default (zero flash-write cost during normal operation), armed only
    // on request via the HTTP server started below once network is up. See
    // flash_log.h -- this can never compete with Matter/Thread for
    // OpenThread message buffers the way Papertrail's UDP traffic did,
    // regardless of how much gets logged, since it never touches the
    // network at all.
    ESP_ERROR_CHECK(flash_log_init());

    // Outage log: small NVS-backed record of what kind of connectivity/state
    // problem happened, when it started, and when it recovered -- see
    // outage_log.h. Independent of flash_log above: this only ever writes on
    // an actual start/end transition (never a timer), and unlike flash_log
    // it's always on, not something that needs to be armed.
    ESP_ERROR_CHECK(outage_log_init());

    // Initialize Matter device and start commissioning. Matter now owns
    // network bring-up (Thread) as part of its normal commissioning flow,
    // rather than the app pre-connecting with baked-in credentials first --
    // see matter_set_network_event_group()/app_chip_event_handler() in
    // matter_device.cpp for how connectivity is signaled back here.
    ESP_LOGI(TAG, "Initializing Matter device...");
    ESP_ERROR_CHECK(matter_device_init());

    matter_set_network_event_group(g_app_event_group, NETWORK_CONNECTED_BIT);

    ESP_LOGI(TAG, "Starting Matter commissioning...");
    ESP_ERROR_CHECK(matter_start_commissioning());

    // Wait for network connectivity before starting network-dependent
    // services. An already-commissioned device reattaches to its stored
    // Thread network within seconds; an uncommissioned device waits here
    // indefinitely for the user to commission it (e.g. via Home Assistant).
    ESP_LOGI(TAG, "Waiting for network connectivity (commission via Home Assistant if not already paired)...");
    EventBits_t bits = 0;
    while (!(bits & NETWORK_CONNECTED_BIT)) {
        bits = xEventGroupWaitBits(g_app_event_group,
                                   NETWORK_CONNECTED_BIT,
                                   pdFALSE,
                                   pdFALSE,
                                   pdMS_TO_TICKS(30000));
        if (!(bits & NETWORK_CONNECTED_BIT)) {
            ESP_LOGI(TAG, "Still waiting for network connectivity...");
        }
    }
    ESP_LOGI(TAG, "Network connectivity established");

    // Outage log, Thread-disconnected detector: register once now that
    // Thread has attached at least once (esp_openthread_get_instance()
    // needs a live instance, which is only guaranteed after this point --
    // Matter owns Thread bring-up internally, see matter_device_init()
    // above). s_thread_was_attached starts true here deliberately, so the
    // callback only reacts to a *future* detach, not this initial connect.
    s_thread_was_attached = true;
    if (esp_openthread_lock_acquire(pdMS_TO_TICKS(1000))) {
        otInstance *instance = esp_openthread_get_instance();
        if (instance) {
            otSetStateChangedCallback(instance, thread_state_changed_cb, NULL);
        } else {
            ESP_LOGW(TAG, "No OpenThread instance yet; Thread-disconnected outage detection not registered");
        }
        esp_openthread_lock_release();
    } else {
        ESP_LOGW(TAG, "Could not acquire OpenThread lock; Thread-disconnected outage detection not registered");
    }

    // On-demand log access -- GET /logs, POST /logs/enable, POST /logs/disable
    // -- reachable at this device's Thread IPv6 address, no physical access
    // needed. Runs regardless of whether logging is currently armed.
    log_server_start();

    // Tuya authentication requires valid system time. Retries indefinitely
    // rather than aborting -- see wait_for_time_sync() for why.
    wait_for_time_sync();

    // Outage log, device-restart detector: record why we're booting, unless
    // this was an expected plain power-on (deliberate reflash or a mains
    // power-cycle, like normal operation) -- only the unexpected reset
    // reasons are worth a record. This needs to run after wait_for_time_sync()
    // above, since it needs a real epoch timestamp to be meaningful.
    esp_reset_reason_t reset_reason = esp_reset_reason();
    if (reset_reason != ESP_RST_POWERON) {
        ESP_LOGW(TAG, "Booted from unexpected reset reason: %d", (int)reset_reason);
        outage_log_record_point_event(OUTAGE_REASON_DEVICE_RESTART, (uint8_t)reset_reason, get_thread_link_rssi());
    }

    // Initialize Tuya client
    ESP_LOGI(TAG, "Initializing Tuya client...");
    ESP_ERROR_CHECK(tuya_client_init(
        TUYA_DEVICE_ID,
        TUYA_CLIENT_ID,
        TUYA_CLIENT_SECRET
    ));

    // Initialize the IR transmitter (PLAN.md Milestone 1/2). Confirmed
    // 2026-09-07 against real hardware (test_apps/ir_live_test) -- an
    // emitter is mounted and the unit responds correctly to transmitted
    // commands.
    ESP_ERROR_CHECK(ir_tcl112_init());

    // Must exist before any of Phase 3's tasks (sync_task/command_task/
    // followme_task) start, since all three can call transmit_ir_state_frame().
    g_ir_send_mutex = xSemaphoreCreateMutex();
    if (!g_ir_send_mutex) {
        ESP_LOGE(TAG, "Failed to create IR send mutex");
        return;
    }

    // Follow-Me's ambient sensor reading (PLAN.md Milestone 3) comes from HA
    // now, via the Follow-Me Matter endpoint (matter_get_followme_ambient_temp_c_x100())
    // -- this firmware's own attempt at a direct MQTT client to Mosquitto
    // never got a working DNS/NAT64 path on this Thread-only device
    // (2026-09-07 decision), so there's no separate client to initialize here.

    // ========== Phase 3: Create Integration Tasks ==========
    
    // Status synchronization task (Tuya → Matter)
    xTaskCreate(sync_task, 
                "status_sync",      // Task name
                16384,              // Stack size
                NULL,               // Parameters
                4,                  // Priority
                NULL);              // Task handle
    
    // Command routing task (Matter → Tuya)
    xTaskCreate(command_task,
                "command_route",
                12288,
                NULL,
                3,                  // Lower priority than sync
                NULL);
    
    // Health monitoring task. 2048 was enough before Papertrail logging
    // existed; confirmed on real hardware (2026-08-31) it is not enough
    // after -- ESP_LOGI now routes through papertrail_vprintf's sendto()
    // call chain (down through lwIP into OpenThread's network stack), which
    // needs real stack depth of its own, apparently more under the error/
    // retry paths OpenThread takes while it's short on message buffers.
    // Caused a genuine stack-protection-fault panic (SP ~300 bytes past the
    // lower bound) in this exact task, right on the first ESP_LOGI call of
    // a health_task cycle, during a burst of OpenThread NoBufs errors. An
    // earlier fix (moving papertrail_logger.c's own formatting buffers off
    // the stack) wasn't sufficient on its own -- the task's total budget
    // just needed to be bigger now that its logging path is heavier.
    xTaskCreate(health_task,
                "health_monitor",
                4096,
                NULL,
                2,
                NULL);

    // Follow-Me task (PLAN.md Milestone 3). Same priority tier as
    // command_task -- it sends IR too, just on its own timer instead of in
    // response to a Matter write.
    xTaskCreate(followme_task,
                "followme",
                8192,
                NULL,
                3,
                NULL);

    ESP_LOGI(TAG, "\n=== MiniSplit Matter Bridge Ready ===");
    ESP_LOGI(TAG, "Status Sync Interval: %ums (fixed)", STATUS_POLL_INTERVAL_MS);
    ESP_LOGI(TAG, "Command Poll Interval: %ums", COMMAND_POLL_INTERVAL_MS);
    ESP_LOGI(TAG, "Status: Waiting for Matter commissioning...\n");
}
