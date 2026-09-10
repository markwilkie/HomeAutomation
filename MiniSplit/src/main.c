#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
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
#include "secrets.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include <openthread/instance.h>
#include <openthread/thread.h>

static const char *TAG = "MAIN";

// TCL112AC protocol mode values (see ../IR_PROTOCOL_REFERENCE.md's "State
// byte map") -- deliberately a separate encoding from Tuya's own "mode" DP
// (0=auto,1=cool,2=dry,3=fan,4=heat, see map_matter_mode_to_tuya() below).
// Don't conflate the two.
#define IR_MODE_HEAT 1
#define IR_MODE_DRY  2
#define IR_MODE_COOL 3
#define IR_MODE_FAN  7
#define IR_MODE_AUTO 8

// TCL112AC protocol Fan values, state[8] bits 0-2 -- our own capture-
// confirmed enum for this specific unit (IR_PROTOCOL_REFERENCE.md's "Fan
// speed discrepancy" section), NOT the generic IRremoteESP8266 library's
// model, which claims a 5th distinct "Quiet" value (1) this unit never
// actually sends. `2` covers both Quiet and Low here -- the two are only
// disambiguated by the Type 2 companion frame's Quiet bit, which this
// project currently always sends as a fixed "everyday" frame (see
// transmit_ir_state_frame()'s kType2CompanionFrame), so Quiet and Low are
// indistinguishable at the IR level from this firmware today regardless of
// which Tuya fan_speed_enum value maps to IR_FAN_LOW below.
#define IR_FAN_AUTO 0
#define IR_FAN_LOW  2
#define IR_FAN_MED  3
#define IR_FAN_HIGH 5

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

// True when the Thermostat's SystemMode was last set to kOff, which we
// implement as Tuya "fan" mode + fresh air open (see command_task) rather
// than a real power-down -- kept running so the blower/fresh-air stays on.
// Tuya's "mode" DP can't distinguish that from a genuine user-selected Fan
// Only, so this locally remembers which reason we're in fan mode for and
// lets map_tuya_mode_to_matter report kOff instead of kFanOnly while it's
// set. Cleared the moment any other explicit mode command is processed.
static bool g_mode_off_via_fan_proxy = false;

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

// Tuya "mode" DP: 0=auto, 1=cool, 2=dry, 3=fan, 4=heat.
// Matter Thermostat SystemModeEnum: kOff=0, kAuto=1, kCool=3, kFanOnly=7, kDry=8, kHeat=4.
static uint8_t map_tuya_mode_to_matter(const tuya_device_status_t *device_status)
{
    if (!device_status->switch_state) {
        return 0; // kOff
    }
    if (device_status->ac_mode == 3 && g_mode_off_via_fan_proxy) {
        return 0; // kOff -- idling in the fan-mode proxy, see g_mode_off_via_fan_proxy above
    }
    switch (device_status->ac_mode) {
        case 0: return 1; // kAuto
        case 1: return 3; // kCool
        case 2: return 8; // kDry
        case 3: return 7; // kFanOnly
        case 4: return 4; // kHeat
        default: return 1; // Unknown Tuya mode value -> Auto
    }
}

// Inverse of map_tuya_mode_to_matter. Returns -1 for Matter modes Tuya's "mode"
// DP has no equivalent for (EmergencyHeat, Precooling, Sleep); kOff is
// special-cased by the caller (command_task's fan-idle proxy) before this
// function is ever called, never routed through here.
static int8_t map_matter_mode_to_tuya(uint8_t matter_mode)
{
    switch (matter_mode) {
        case 1: return 0; // kAuto -> auto
        case 3: return 1; // kCool -> cool
        case 8: return 2; // kDry -> dry
        case 7: return 3; // kFanOnly -> fan
        case 4: return 4; // kHeat -> heat
        default: return -1;
    }
}

// Matter SystemModeEnum -> the IR protocol's own mode value (see
// ../IR_PROTOCOL_REFERENCE.md). NOT the same numeric mapping as
// map_matter_mode_to_tuya() above -- the two protocols don't share an
// encoding. Returns -1 for modes with no IR equivalent (same set
// map_matter_mode_to_tuya() rejects); kOff is handled by the caller (maps
// to IR_MODE_FAN, matching the existing Tuya-path fan-idle-proxy precedent
// -- see command_task's System Mode block).
static int8_t map_matter_mode_to_ir(uint8_t matter_mode)
{
    switch (matter_mode) {
        case 1: return IR_MODE_AUTO;
        case 3: return IR_MODE_COOL;
        case 8: return IR_MODE_DRY;
        case 7: return IR_MODE_FAN;
        case 4: return IR_MODE_HEAT;
        default: return -1;
    }
}

// Tuya's fan_speed_enum (0-7: Stop/Mute/Low/Med-Low/Med/Med-High/High/
// Turbo, TUYA_DP_REFERENCE.md) -> the IR protocol's own 4-value Fan enum
// (IR_FAN_* above). Not a 1:1 mapping -- Tuya exposes finer granularity
// than this unit's IR protocol actually has, so this collapses each Tuya
// step to the closest real IR level rather than inventing bit positions
// that don't exist. HA/Matter can't select fan speed at all today (out of
// scope per PLAN.md Milestone 2), so this only matters for *preserving*
// whatever speed was last set via the physical remote or the Tuya app
// across an unrelated IR command (see build_ir_state_frame()'s "Preserve
// fields HA doesn't control" note) -- not for controlling it.
static uint8_t map_tuya_fan_speed_to_ir(uint8_t tuya_fan_speed)
{
    switch (tuya_fan_speed) {
        case 0: return IR_FAN_AUTO; // Stop
        case 1: return IR_FAN_LOW;  // Mute
        case 2: return IR_FAN_LOW;  // Low
        case 3: return IR_FAN_LOW;  // Med-Low
        case 4: return IR_FAN_MED;  // Med
        case 5: return IR_FAN_MED;  // Med-High
        case 6: return IR_FAN_HIGH; // High
        case 7: return IR_FAN_HIGH; // Turbo
        default: return IR_FAN_AUTO;
    }
}

// Builds and transmits one full TCL112AC IR frame reflecting the AC's
// best-known current state, with exactly one field overridden (whichever
// this specific command is actually changing -- mode or setpoint, never
// both at once since that's not how the Matter attributes arrive). Callers
// are responsible for refreshing `status` from a fresh, on-demand Tuya GET
// immediately beforehand (see command_task) -- this function only builds
// and sends, it doesn't fetch, so the pre-send-refresh timing described in
// PLAN.md Milestone 2 stays visible at the call site rather than hidden in
// here.
//
// KNOWN LIMITATION, deliberate for now: Swing(V/H), Health, and Fresh Air
// aren't preserved from the unit's actual live state -- every frame sent
// from here carries the base template's fixed captured values for those
// fields (see kBaseFrame-equivalent literal below), which could revert real
// out-of-band changes (real remote, Tuya app) back to that fixed snapshot
// rather than zeroing them outright as an earlier version of this function
// did. Swing/Health are a deliberate user-call deferral (see
// IR_PROTOCOL_REFERENCE.md's "Known gaps"); Fresh Air's IR bit position is
// still genuinely unknown (capture attempted and abandoned). Fan speed and
// Light are both preserved now (2026-09-09 and 2026-09-07 respectively) --
// this comment previously listed them here too but that went stale. This
// IS a real-world risk for the fields still unpreserved: as of 2026-09-07
// an IR emitter is mounted and confirmed transmitting commands the unit
// actually accepts (test_apps/ir_live_test).
// Builds the frame array only (no send) -- shared by send_ir_frame() below
// and send_followme_frame() (Follow-Me heartbeat), since both need the same
// base-template-plus-known-fields construction and only differ in which
// extra bits/bytes they layer on afterward.
static void build_ir_state_frame(const tuya_device_status_t *status, bool power_on,
                                   bool override_mode, uint8_t override_ir_mode,
                                   bool override_setpoint, int16_t override_setpoint_c_x100,
                                   uint8_t out_frame[IR_TCL112_FRAME_LEN],
                                   uint8_t *out_ir_mode, int16_t *out_setpoint_c)
{
    uint8_t ir_mode;
    if (override_mode) {
        ir_mode = override_ir_mode;
    } else {
        uint8_t matter_mode = map_tuya_mode_to_matter(status);
        int8_t mapped = map_matter_mode_to_ir(matter_mode);
        ir_mode = (mapped >= 0) ? (uint8_t)mapped : IR_MODE_AUTO;
    }

    int16_t setpoint_c_x100 = override_setpoint ? override_setpoint_c_x100 : status->temp_set;
    // Round to the nearest whole degree C, don't truncate -- plain integer
    // division here silently biased every setpoint down by up to almost a
    // full degree C (nearly 2F), found via live HA testing 2026-09-07:
    // selecting 73F in HA (~22.78C) truncated to 22C (71.6F), which the
    // unit's own display then rounded down to 72F. Setpoints here are
    // always positive (16-31C range enforced below), so a simple +50
    // half-up offset before truncating is exact -- no negative-number edge
    // case to handle.
    int16_t setpoint_c = (int16_t)((setpoint_c_x100 + 50) / 100);
    if (setpoint_c < 16) {
        setpoint_c = 16;
    } else if (setpoint_c > 31) {
        setpoint_c = 31;
    }

    // Base/template frame -- updated 2026-09-09 to a fresher real capture
    // (Power: On, Mode: Cool, Temp: 21C, Fan: Auto, Light: On,
    // Swing/Econo/Health/Turbo/Timers all off), replacing the original
    // 2026-09-07 Fan/20C capture. See IR_PROTOCOL_REFERENCE.md's "Base/
    // template frame" section and
    // ../MiniSplitIR/captures/protocol_capture.md's 2026-09-09 re-capture
    // session for the full writeup. Byte-for-byte equivalent to the old
    // template for every field this function doesn't overwrite (Timers,
    // Econo, Health, Turbo, SwingV, Follow-Me flag, isTcl/toggle) --
    // swapping it is not expected to change on-wire behavior, only the
    // documentation trail; kept as a real capture rather than `{0}` for the
    // same reason as before. Construct every outgoing command from this
    // array, overwriting only the fields this project actually controls
    // (Power, Mode, Setpoint below) -- every field this project doesn't
    // model yet rides along as a real captured default instead of a guess.
    static const uint8_t kBaseFrame[IR_TCL112_FRAME_LEN] = {
        0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x03, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x84, 0xCA,
    };
    memcpy(out_frame, kBaseFrame, IR_TCL112_FRAME_LEN);

    // Power bit -- confirmed 2026-09-04 (see IR_PROTOCOL_REFERENCE.md's state
    // byte map). Driven by the caller now that OnOff is wired to real IR
    // (2026-09-07) instead of always forcing it on: the Desired Setpoint and
    // System Mode call sites still always pass power_on=true (System Mode's
    // kOff case idles in Fan mode rather than actually powering down -- see
    // that call site's comment), but OnOff needs to actually turn the unit
    // off. Getting this wrong the same way once already: leaving it clear
    // unconditionally (when frame[5] came from `{0}`) silently sent "Power
    // Off" as part of every command's full-state frame, found via live HA
    // testing 2026-09-07 -- a Desired Setpoint change transmitted without
    // error but the unit never visibly responded.
    if (power_on) {
        out_frame[5] |= 0x04;
    } else {
        out_frame[5] &= (uint8_t)~0x04;
    }

    // Light -- state[5] bit 0x40, sourced-but-unconfirmed polarity per
    // IR_PROTOCOL_REFERENCE.md ("Inverted: ... bit clear = light on, bit set
    // = light off"). Previously left at the base template's fixed captured
    // value, which forced the unit's Light to that one snapshot on every
    // single send regardless of its real current setting -- confirmed as a
    // real, live regression via HA testing 2026-09-07 (Light reverted to
    // off after an unrelated Desired Setpoint change). Now driven from the
    // same pre-send Tuya refresh this function already receives, same as
    // Mode/Setpoint above.
    if (status->light) {
        out_frame[5] &= (uint8_t)~0x40;
    } else {
        out_frame[5] |= 0x40;
    }

    // Fan -- state[8] bits 0-2, see map_tuya_fan_speed_to_ir() above. Bits
    // 3-7 (SwingV, TimerIndicator, the unclaimed bit 7) are left as the
    // base template's captured values, same "not preserved yet" limitation
    // as Swing/Health/Fresh Air below.
    out_frame[8] = (uint8_t)((out_frame[8] & ~0x07) | (map_tuya_fan_speed_to_ir(status->fan_speed) & 0x07));

    out_frame[6] = (uint8_t)((out_frame[6] & ~0x0F) | (ir_mode & 0x0F));  // Mode nibble; bits 4-7 preserved from base
    out_frame[7] = (uint8_t)(31 - setpoint_c);
    // frame[12] (isTcl + toggle bit) and frame[13] (checksum, recomputed
    // fresh by ir_tcl112_send()) are left as the base template's values.
    //
    // Fresh Air (Tuya's fresh_air_valve, already read into `status` below)
    // is NOT preserved here despite being available -- its IR bit position
    // is still genuinely unconfirmed (IR_PROTOCOL_REFERENCE.md's Known Gaps:
    // "attempted 2026-09-07, abandoned, still unresolved... out of scope for
    // Milestone 2"). Guessing a bit for it risks corrupting some other,
    // currently-working field. Every send still reverts Fresh Air to the
    // base template's captured value until that bit is actually found.

    *out_ir_mode = ir_mode;
    *out_setpoint_c = setpoint_c;
}

// Timestamp of the last successful IR transmission of any kind (regular
// command or Follow-Me heartbeat) -- used by followme_task to delay its next
// heartbeat tick by one full interval after a real command, per PLAN.md
// Milestone 2's note, so a heartbeat doesn't immediately follow (and
// potentially race/duplicate) a just-sent command.
static TickType_t g_last_ir_send_tick = 0;

// Sends the Type 2 companion frame + the given Type 1 frame (already built
// by build_ir_state_frame() or send_followme_frame()), logging both. Shared
// by send_ir_frame() and send_followme_frame() -- every full-state send
// needs this same two-frame sequence (see IR_PROTOCOL_REFERENCE.md's "Type 2
// frame" section: confirmed required, not optional, via test_apps/ir_live_test).
static esp_err_t transmit_ir_state_frame(uint8_t frame[IR_TCL112_FRAME_LEN])
{
    // state[6] is a real capture-confirmed free-running step counter that
    // doesn't gate acceptance (see ../MiniSplitIR/captures/protocol_capture.md),
    // so this fixed, verbatim real capture is enough -- no need to reproduce
    // its exact sequence.
    static const uint8_t kType2CompanionFrame[IR_TCL112_FRAME_LEN] = {
        0x23, 0xCB, 0x26, 0x02, 0x00, 0x40, 0x20, 0x00, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x48,
    };
    uint8_t type2_frame[IR_TCL112_FRAME_LEN];
    memcpy(type2_frame, kType2CompanionFrame, IR_TCL112_FRAME_LEN);

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
        return type2_err;
    }

    esp_err_t err = ir_tcl112_send(frame);
    if (err == ESP_OK) {
        g_last_ir_send_tick = xTaskGetTickCount();
    } else {
        ESP_LOGE(TAG, "ir_tcl112_send failed: %s", esp_err_to_name(err));
    }
    return err;
}

// See build_ir_state_frame()'s doc comment for the base-template/known-
// limitation notes that apply here too. Callers are responsible for
// refreshing `status` from a fresh, on-demand Tuya GET immediately
// beforehand (see command_task) -- this function only builds and sends.
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

    esp_err_t err = transmit_ir_state_frame(frame);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "IR frame sent: power=%s mode=%u setpoint=%dC",
                 power_on ? "on" : "off", ir_mode, setpoint_c);
    }
    return err;
}

// Follow-Me heartbeat/enable frame (PLAN.md Milestone 3). Reflects the same
// Power/Mode/Setpoint/Light as a regular command (via build_ir_state_frame(),
// no overrides -- Follow-Me doesn't change any of those, just layers its own
// bits on top), plus:
//   - state[4]/state[6] bit 0x80: Follow-Me enabled (state[6]'s copy is a
//     real capture-confirmed mirror of state[4]'s, not independently
//     meaningful on its own).
//   - state[5] bit 0x20: set for the first frame after Follow-Me was last
//     inactive ("enable" instance), clear for every subsequent periodic
//     re-send ("heartbeat") -- see IR_PROTOCOL_REFERENCE.md's "Follow Me
//     behavior" section.
//   - state[11]: ambient sensor temp, whole degrees C.
// g_followme_active tracks which of those two instance types this call is;
// followme_task resets it to false whenever a tick is skipped (no sensor
// reading, no confirmed Tuya state), so resuming after a gap is treated as a
// fresh enable rather than a continued heartbeat.
static bool g_followme_active = false;

static esp_err_t send_followme_frame(const tuya_device_status_t *status, int8_t ambient_temp_c)
{
    uint8_t frame[IR_TCL112_FRAME_LEN];
    uint8_t ir_mode;
    int16_t setpoint_c;
    build_ir_state_frame(status, true, false, 0, false, 0, frame, &ir_mode, &setpoint_c);

    bool is_enable_instance = !g_followme_active;

    frame[4] |= 0x80;
    frame[6] |= 0x80;
    if (is_enable_instance) {
        frame[5] |= 0x20;
    } else {
        frame[5] &= (uint8_t)~0x20;
    }
    frame[11] = (uint8_t)ambient_temp_c;

    esp_err_t err = transmit_ir_state_frame(frame);
    if (err == ESP_OK) {
        g_followme_active = true;
        ESP_LOGI(TAG, "Follow-Me %s sent: ambient=%dC mode=%u setpoint=%dC",
                 is_enable_instance ? "enable" : "heartbeat", ambient_temp_c, ir_mode, setpoint_c);
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

static void cache_and_apply_status(const tuya_device_status_t *device_status)
{
    g_last_device_status = *device_status;
    g_last_device_status_valid = true;
    apply_status_to_matter(device_status);
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
                if (g_sync_state.status_poll_failures > 0) {
                    // Was failing, now succeeded -- outage over.
                    outage_log_end(OUTAGE_REASON_TUYA_UNREACHABLE);
                }
                g_sync_state.status_poll_failures = 0;  // Reset failure counter
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

        // Desired-setpoint mismatch detection: the standalone Desired
        // Setpoint Matter endpoint (matter_get_desired_cooling_setpoint(),
        // HA-writable, never touched by this task) is compared against what
        // Tuya just reported, in whole-Fahrenheit-degree terms -- comparing
        // raw Celsius is unreliable here since Tuya's temp_set only stores
        // 0.5C steps, so a whole-Fahrenheit command doesn't generally
        // round-trip back to an exact Celsius match even once genuinely
        // applied (see tuya_setpoint_c_to_f()'s doc comment).
        //
        // Re-added 2026-09-09: does send an IR correction on a genuine
        // mismatch, gated by a >1F tolerance rather than exact equality.
        // An earlier version corrected on any exact mismatch and was
        // removed 2026-09-07 because it fired a real IR command on every
        // single boot: a persisted NVS "desired" value essentially never
        // matches Tuya's independently-derived temp_set_f by exact
        // coincidence (their C->F rounding conventions can legitimately
        // disagree by a degree even when the AC is genuinely at the
        // requested temperature), so exact-match "correction" would have
        // kept firing every 5-minute poll indefinitely, not just once per
        // boot. The >1F tolerance absorbs that class of rounding-convention
        // noise while still catching and correcting a real, larger
        // divergence (e.g. a command that silently failed to land, or an
        // out-of-band change back toward a stale setpoint).
        int16_t desired_c_x100 = matter_get_desired_cooling_setpoint();
        int16_t desired_f_for_outage_check = tuya_setpoint_c_to_f(desired_c_x100);
        int16_t setpoint_mismatch_f = (int16_t)abs(desired_f_for_outage_check - device_status.temp_set_f);
        if (setpoint_mismatch_f > 1) {
            outage_log_start(OUTAGE_REASON_SETPOINT_MISMATCH, get_thread_link_rssi());

            ESP_LOGW(TAG, "Setpoint mismatch %dF beyond tolerance (desired %dF, Tuya reports %dF) -- sending IR correction",
                     setpoint_mismatch_f, desired_f_for_outage_check, device_status.temp_set_f);
            esp_err_t send_err = send_ir_frame(&device_status, true, false, 0, true, desired_c_x100);
            if (send_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send setpoint correction via IR: %s", esp_err_to_name(send_err));
            }
        } else {
            outage_log_end(OUTAGE_REASON_SETPOINT_MISMATCH);
        }
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

        tuya_device_status_t status = g_last_device_status;
        if (tuya_get_device_status(&status) != ESP_OK) {
            ESP_LOGW(TAG, "Post-send verify poll %d/%d: Tuya GET failed, retrying",
                     attempt, POST_SEND_VERIFY_MAX_ATTEMPTS);
            continue;
        }

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
 * minimal-surface approach elsewhere. Waits for a full interval before its
 * first send so early boot noise (before the first sync_task poll / before
 * HA has pushed a reading yet) doesn't force a send off stale/default state.
 */
static void followme_task(void *param)
{
    ESP_LOGI(TAG, "Follow-Me task started (interval: %ums)", FOLLOWME_HEARTBEAT_INTERVAL_MS);

    while (1) {
        // Re-derive the remaining wait from g_last_ir_send_tick every time
        // we wake, rather than a single fixed vTaskDelay -- this is what
        // makes a command sent from command_task actually delay the next
        // heartbeat by a full interval (PLAN.md Milestone 2's note), since
        // transmit_ir_state_frame() bumps that same timestamp on every
        // successful send, command or heartbeat alike.
        TickType_t interval_ticks = pdMS_TO_TICKS(FOLLOWME_HEARTBEAT_INTERVAL_MS);
        TickType_t elapsed = xTaskGetTickCount() - g_last_ir_send_tick;
        if (elapsed < interval_ticks) {
            vTaskDelay(interval_ticks - elapsed);
            continue;
        }

        int16_t ambient_c_x100;
        if (!matter_get_followme_ambient_temp_c_x100(&ambient_c_x100)) {
            ESP_LOGW(TAG, "Follow-Me: no ambient sensor reading from HA yet, skipping this tick");
            // Treat the next successful reading as a fresh enable, not a
            // continued heartbeat -- see send_followme_frame()'s doc comment.
            g_followme_active = false;
            vTaskDelay(interval_ticks);
            continue;
        }
        // Round to the nearest whole degree C -- state[11] only carries
        // whole-degree values (see IR_PROTOCOL_REFERENCE.md's Follow Me
        // section), same reasoning as build_ir_state_frame()'s setpoint
        // rounding.
        int8_t ambient_c = (int8_t)((ambient_c_x100 >= 0 ? ambient_c_x100 + 50 : ambient_c_x100 - 50) / 100);
        if (!g_last_device_status_valid) {
            ESP_LOGW(TAG, "Follow-Me: no confirmed Tuya state yet, skipping this tick");
            vTaskDelay(interval_ticks);
            continue;
        }

        esp_err_t err = send_followme_frame(&g_last_device_status, ambient_c);
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
            tuya_device_status_t refreshed_status = g_last_device_status;
            if (tuya_get_device_status(&refreshed_status) == ESP_OK) {
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
            if (refreshed_status.switch_state == desired_onoff) {
                ESP_LOGI(TAG, "Power already %s per fresh Tuya status, skipping redundant IR send",
                         desired_onoff ? "on" : "off");
                matter_clear_onoff_command();
            } else {
                esp_err_t result = send_ir_frame(&refreshed_status, desired_onoff, false, 0, false, 0);
                if (result != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send power command via IR: %s", esp_err_to_name(result));
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
            tuya_device_status_t refreshed_status = g_last_device_status;
            if (tuya_get_device_status(&refreshed_status) == ESP_OK) {
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

            uint8_t expected_tuya_mode;
            uint8_t ir_mode;
            if (mode_cmd == 0) {
                // kOff from HA/Matter: rather than powering the unit fully
                // down, idle in Fan mode instead -- keeps the indoor blower
                // usable, just without active cooling/heating. Originally a
                // Tuya-only workaround (tuya_set_power(false) would also stop
                // the fresh-air fan); kept even now that the Power bit is
                // confirmed and OnOff is wired to real IR, since a real
                // "Power Off" via IR would still lose Fresh Air the same way
                // -- Fresh Air's own IR bit is still unconfirmed (see
                // send_ir_frame()'s doc comment), so there's no way yet to
                // command "off but keep Fresh Air" other than staying in Fan
                // mode with power on.
                expected_tuya_mode = 3;
                ir_mode = IR_MODE_FAN;
            } else {
                int8_t tuya_mode = map_matter_mode_to_tuya(mode_cmd);
                int8_t mapped_ir_mode = map_matter_mode_to_ir(mode_cmd);
                if (tuya_mode < 0 || mapped_ir_mode < 0) {
                    ESP_LOGW(TAG, "Matter mode %u has no IR/Tuya equivalent, ignoring", mode_cmd);
                    matter_clear_mode_command();
                    continue;
                }
                expected_tuya_mode = (uint8_t)tuya_mode;
                ir_mode = (uint8_t)mapped_ir_mode;
            }

            // Pre-send refresh (PLAN.md Milestone 2), same reasoning as the
            // Desired Setpoint block above: fetch Tuya's latest status right
            // before building the IR frame, so the setpoint byte this frame
            // preserves reflects any out-of-band change instead of a
            // possibly-stale cached value.
            tuya_device_status_t refreshed_status = g_last_device_status;
            if (tuya_get_device_status(&refreshed_status) == ESP_OK) {
                cache_and_apply_status(&refreshed_status);
                g_sync_state.last_status_update = xTaskGetTickCount();
            } else {
                ESP_LOGW(TAG, "Pre-send Tuya refresh failed; using last known state for the IR frame's setpoint byte");
            }

            // Dedup against the freshly-refreshed shadow state (PLAN.md
            // Milestone 2): if Tuya already reports the mode this command
            // asks for, skip the redundant IR send and its ~90s post-send
            // verify loop. Still apply the fan-idle-proxy latch update below
            // either way -- that's tracking *why* the mode is what it is
            // (an explicit HA request vs. this device's own Off-via-Fan
            // workaround), which is true regardless of whether an IR frame
            // actually needed to go out this time.
            bool mode_already_matches = (refreshed_status.ac_mode == expected_tuya_mode);
            if (mode_already_matches) {
                ESP_LOGI(TAG, "Mode already matches per fresh Tuya status, skipping redundant IR send");
            } else {
                // power_on=true unconditionally: even the kOff case idles in
                // Fan mode rather than actually powering down (see above),
                // and a genuine mode selection obviously wants the unit
                // running.
                esp_err_t result = send_ir_frame(&refreshed_status, true, true, ir_mode, false, 0);
                if (result != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send mode command via IR: %s", esp_err_to_name(result));
                }
            }

            // Any explicit mode command -- including a genuine Fan Only
            // selection -- reflects the user's real intent from here on, so
            // it always overrides the fan-idle-proxy latch.
            g_mode_off_via_fan_proxy = (mode_cmd == 0);
            ESP_LOGI(TAG, "Mode command processed (ir_mode=%u)%s%s", ir_mode,
                     g_mode_off_via_fan_proxy ? " [Off via fan-idle proxy]" : "",
                     mode_already_matches ? " [already matched, no IR sent]" : "");

            matter_clear_mode_command();

            if (!mode_already_matches) {
                post_send_verify_and_sync(true, true, false, 0, true, expected_tuya_mode);
            }
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
    ESP_LOGI(TAG, "\n\n=== MiniSplit Matter Bridge Starting ===\n");
    
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
