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

#include "tuya_client.h"
#include "matter_device.h"
#include "papertrail_logger.h"
#include "flash_log.h"
#include "log_server.h"
#include "bme280.h"
#include "secrets.h"

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
#define ENV_POLL_INTERVAL_MS 30000        // Read BME280 every 30 seconds
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

// Same idea as SYNC_STALL_RESTART_MS, but for env_task/BME280 -- a much
// shorter bound since its normal cadence is ENV_POLL_INTERVAL_MS (30s), not
// 5 minutes. bme280.c already has its own I2C bus-recovery logic that
// handles an ordinary wedged bus within ~90s (BME280_RECOVERY_THRESHOLD),
// so this threshold only needs to be generous enough to never fire during
// that normal recovery -- it exists specifically for the case confirmed on
// real hardware where bme280_read() itself never returns at all (an 8.5-hour
// stall, zero log output the entire time), which silently bypasses that
// recovery logic since it can only count a failure once a call returns.
#define ENV_STALL_RESTART_MS (5 * 60 * 1000)    // 5 minutes

// State tracking for error recovery
typedef struct {
    uint32_t last_status_update;        // Timestamp of last successful status update
    uint32_t last_command_check;        // Timestamp of last command check
    uint32_t last_env_heartbeat;        // Timestamp of last env_task loop iteration (see below)
    uint8_t status_poll_failures;       // Consecutive failures
    uint8_t network_disconnects;        // Count of connectivity drops
} sync_state_t;

static sync_state_t g_sync_state = {0};

static int16_t normalize_tuya_setpoint(int16_t temp_c)
{
    // Delegates to the shared helper so this matches exactly what
    // tuya_set_temperature() actually sends -- see its doc comment in
    // tuya_client.h. sync_task's desired-setpoint reconciliation sends this
    // return value to Tuya whenever it disagrees with Tuya's own poll.
    return tuya_normalize_setpoint_c(temp_c);
}

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
// DP has no equivalent for (EmergencyHeat, Precooling, Sleep); kOff is handled
// by the caller via tuya_set_power(), not this mapping.
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

static void apply_status_to_matter(const tuya_device_status_t *device_status)
{
    matter_update_onoff(device_status->switch_state);
    vTaskDelay(pdMS_TO_TICKS(MATTER_UPDATE_BURST_SPACING_MS));
    // Mini-split's own indoor reading -- the Thermostat's LocalTemperature.
    // The BME280's independent indoor reading (if fitted) lives on its own
    // Temperature Sensor endpoint instead; see env_task/matter_update_aux_temperature.
    matter_update_local_temperature(device_status->temp_current);
    vTaskDelay(pdMS_TO_TICKS(MATTER_UPDATE_BURST_SPACING_MS));
    matter_update_heating_setpoint(device_status->temp_set);
    vTaskDelay(pdMS_TO_TICKS(MATTER_UPDATE_BURST_SPACING_MS));
    matter_update_cooling_setpoint(device_status->temp_set);
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

        // Desired-setpoint reconciliation: the standalone Desired Setpoint
        // Matter endpoint (matter_get_desired_cooling_setpoint(), HA-writable,
        // never touched by this task) is compared against what Tuya just
        // reported, in whole-Fahrenheit-degree terms -- comparing raw Celsius
        // is unreliable here since Tuya's temp_set only stores 0.5C steps, so
        // a whole-Fahrenheit command doesn't generally round-trip back to an
        // exact Celsius match even once genuinely applied (see
        // tuya_setpoint_c_to_f()'s doc comment). If they disagree, send it
        // again. No pending/expectation state: if this attempt doesn't stick
        // either, the next poll sees that plainly and this same check just
        // tries again -- self-healing by construction rather than by retry
        // bookkeeping.
        int16_t desired_f = tuya_setpoint_c_to_f(matter_get_desired_cooling_setpoint());
        if (desired_f != device_status.temp_set_f) {
            int16_t desired_c_normalized = normalize_tuya_setpoint(matter_get_desired_cooling_setpoint());
            ESP_LOGI(TAG, "Desired setpoint (%dF) differs from Tuya's (%dF), sending update",
                     desired_f, device_status.temp_set_f);
            esp_err_t set_result = tuya_set_temperature(desired_c_normalized);
            if (set_result != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send desired setpoint to Tuya; will retry next poll");
            }
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

            esp_err_t result = tuya_set_power(desired_onoff);
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send power command to Tuya");
            } else {
                ESP_LOGI(TAG, "Power command sent successfully");
            }

            matter_clear_onoff_command();
        }

        // ===== Check for a Desired Setpoint change from Matter =====
        // Added 2026-08-30: sync_task's own reconciliation (below) still
        // runs regardless and is what actually keeps things correct if this
        // ever fails or gets missed -- this block only exists so the common,
        // successful case doesn't have to wait for sync_task's next (up to
        // 5-minute) status poll. Deliberately handled here rather than
        // inline in the Matter attribute callback: that callback runs in the
        // Matter/CHIP stack's own context, and tuya_set_temperature() is a
        // blocking HTTPS call (now bounded at TUYA_HTTP_TIMEOUT_MS, but still
        // multiple seconds in the normal case) -- blocking that callback
        // directly risks stalling Matter's own event processing. Routing
        // through command_task's existing 5-second poll keeps every real
        // Tuya API call serialized through the one task that already owns
        // g_tuya_ctx's shared state (access token, etc.), avoiding any
        // concurrent-access race with sync_task's own calls.
        if (matter_get_desired_setpoint_command_pending()) {
            int16_t desired_c_normalized = normalize_tuya_setpoint(matter_get_desired_cooling_setpoint());
            ESP_LOGI(TAG, "Desired setpoint changed via Matter, sending to Tuya now: %dC (x100)",
                     desired_c_normalized);

            esp_err_t result = tuya_set_temperature(desired_c_normalized);
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send desired setpoint to Tuya; sync_task's next poll will retry");
            } else {
                ESP_LOGI(TAG, "Desired setpoint sent successfully");

                // Immediately re-fetch status so EP1's mirrored setpoint/temp
                // (and the Tuya app) reflect the change within seconds rather
                // than waiting for sync_task's next (up to 5-minute) poll --
                // this was the recurring source of "Tuya app doesn't match"
                // confusion. This is a GET triggered only on a real, already-
                // rare setpoint change (not a blanket faster polling interval),
                // so it doesn't meaningfully add to Tuya API quota usage.
                // Tuya's backend may not have fully applied the SET yet, so a
                // mismatch here is expected/benign -- sync_task's own
                // reconciliation (above) is still what guarantees convergence.
                tuya_device_status_t refreshed_status = {0};
                if (tuya_get_device_status(&refreshed_status) == ESP_OK) {
                    cache_and_apply_status(&refreshed_status);
                    g_sync_state.last_status_update = xTaskGetTickCount();
                } else {
                    ESP_LOGW(TAG, "Post-setpoint-change status refresh failed; sync_task's next poll will catch it");
                }
            }

            matter_clear_desired_setpoint_command_pending();
        }

        // ===== Check for System Mode command =====
        uint8_t mode_cmd = matter_get_system_mode_command();
        if (mode_cmd != 0xFF) {  // 0xFF = no command
            ESP_LOGI(TAG, "Processing mode command from controller: %u", mode_cmd);

            esp_err_t result;
            uint8_t expected_tuya_mode;
            if (mode_cmd == 0) {
                // kOff from HA/Matter: rather than powering the unit fully
                // down (tuya_set_power(false)), which would also stop the
                // fresh-air intake fan, switch to Tuya's "fan" mode instead.
                // This keeps the indoor blower running and the fresh-air
                // valve usable, just without active cooling -- matches how
                // the BME280 thermostat cycles "off" during normal setpoint
                // control, where a full power-down each cycle isn't wanted.
                expected_tuya_mode = 3;
                result = tuya_set_mode(expected_tuya_mode);
                if (result == ESP_OK) {
                    tuya_set_fresh_air(true);
                }
            } else {
                int8_t tuya_mode = map_matter_mode_to_tuya(mode_cmd);
                if (tuya_mode < 0) {
                    ESP_LOGW(TAG, "Matter mode %u has no Tuya equivalent, ignoring", mode_cmd);
                    matter_clear_mode_command();
                    continue;
                }
                expected_tuya_mode = (uint8_t)tuya_mode;

                // Selecting a real operating mode implies the unit should be
                // running -- without this, map_tuya_mode_to_matter always
                // reports Off while switch_state is false regardless of
                // ac_mode, so a mode command sent while the unit is off has
                // no visible effect in the driver. Only fires when we last
                // saw it off, to avoid a redundant Tuya call otherwise.
                if (g_last_device_status_valid && !g_last_device_status.switch_state) {
                    esp_err_t power_result = tuya_set_power(true);
                    if (power_result == ESP_OK) {
                        ESP_LOGI(TAG, "Powering on unit to apply mode command");
                    } else {
                        ESP_LOGW(TAG, "Failed to power on unit before mode command");
                    }
                }

                result = tuya_set_mode(expected_tuya_mode);
            }

            if (result != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send mode command to Tuya");
            } else {
                // Any explicit mode command -- including a genuine Fan Only
                // selection -- reflects the user's real intent from here on,
                // so it always overrides the fan-idle-proxy latch.
                g_mode_off_via_fan_proxy = (mode_cmd == 0);
                ESP_LOGI(TAG, "Mode command sent (mode=%u)%s", expected_tuya_mode,
                         g_mode_off_via_fan_proxy ? " [Off via fan-idle proxy]" : "");
            }

            matter_clear_mode_command();
        }
    }
}

// Confirmed on real hardware: an intermittent corrupted I2C read occasionally
// reports a physically implausible single-sample value (isolated ~20-40F
// spikes that revert the very next 30s poll). This is a bad byte in transit
// during the burst-read transaction, downstream of the sensor's own
// measurement/filtering -- no BME280 sampling config can fix it, so instead
// every fresh reading is sanity-checked against absolute physical limits and
// against the last known-good reading before it's ever published to
// Matter/HA. The delta thresholds are picked well above normal sample-to-
// sample noise but far below the corruption spikes actually observed.
#define BME280_TEMP_MIN_C -40.0f
#define BME280_TEMP_MAX_C 85.0f
#define BME280_TEMP_MAX_DELTA_C 5.0f          // ~9F per ENV_POLL_INTERVAL_MS
#define BME280_HUMIDITY_MAX_DELTA_PCT 20.0f

// Confirmed on real hardware: rejecting against the last-good baseline alone
// has no way to recover from a GENUINE environment change (device physically
// relocated, or just a real swing bigger than one poll interval "should"
// allow) -- since the baseline only ever updates on acceptance, a real
// change that differs enough from the stale baseline gets rejected forever,
// stuck for 7+ minutes or more until a reboot resets it. A corrupted I2C
// read is isolated and reverts the very next 30s poll (see the comment on
// bme280_reading_is_plausible() below); a real change stays consistent
// across repeated polls. This threshold is how many consecutive rejections
// have to agree with EACH OTHER (not the stale baseline) before treating it
// as real and force-accepting it as the new baseline.
#define BME280_CONSISTENT_REJECT_THRESHOLD 3

static bool g_last_good_bme280_valid = false;
static float g_last_good_bme280_temp_c = 0.0f;
static float g_last_good_bme280_humidity_pct = 0.0f;

static bool g_pending_bme280_valid = false;
static float g_pending_bme280_temp_c = 0.0f;
static float g_pending_bme280_humidity_pct = 0.0f;
static uint8_t g_pending_bme280_streak = 0;

static bool bme280_reading_is_plausible(const bme280_reading_t *reading)
{
    if (reading->temperature_c < BME280_TEMP_MIN_C || reading->temperature_c > BME280_TEMP_MAX_C ||
        reading->humidity_pct < 0.0f || reading->humidity_pct > 100.0f) {
        g_pending_bme280_streak = 0;
        return false;
    }
    if (!g_last_good_bme280_valid) {
        return true;
    }
    bool within_delta =
        fabsf(reading->temperature_c - g_last_good_bme280_temp_c) <= BME280_TEMP_MAX_DELTA_C &&
        fabsf(reading->humidity_pct - g_last_good_bme280_humidity_pct) <= BME280_HUMIDITY_MAX_DELTA_PCT;
    if (within_delta) {
        g_pending_bme280_streak = 0;
        return true;
    }

    bool matches_pending = g_pending_bme280_valid &&
        fabsf(reading->temperature_c - g_pending_bme280_temp_c) <= BME280_TEMP_MAX_DELTA_C &&
        fabsf(reading->humidity_pct - g_pending_bme280_humidity_pct) <= BME280_HUMIDITY_MAX_DELTA_PCT;

    g_pending_bme280_temp_c = reading->temperature_c;
    g_pending_bme280_humidity_pct = reading->humidity_pct;
    g_pending_bme280_valid = true;
    g_pending_bme280_streak = matches_pending ? (uint8_t)(g_pending_bme280_streak + 1) : 1;

    if (g_pending_bme280_streak >= BME280_CONSISTENT_REJECT_THRESHOLD) {
        ESP_LOGW(TAG, "BME280 reading has repeated %u times despite differing from last known-good "
                      "(temp=%.2fC hum=%.1f%%) -- accepting as a real environment change, not corruption",
                 g_pending_bme280_streak, reading->temperature_c, reading->humidity_pct);
        g_pending_bme280_streak = 0;
        g_pending_bme280_valid = false;
        return true;
    }

    return false;
}

/**
 * @brief Environment sensor task: reads BME280 and updates the standalone
 *        Matter temperature + humidity sensor endpoints.
 */
static void env_task(void *param)
{
    ESP_LOGI(TAG, "Environment sensor task started (BME280, interval: %ums)",
             ENV_POLL_INTERVAL_MS);

    while (1) {
        // Stamped at the top of every iteration, before the read attempt --
        // not on success -- so a genuine hang inside bme280_read() itself
        // (confirmed on real hardware: an 8.5-hour stall with zero log
        // output, meaning the call never returned at all despite its own
        // internal timeouts, silently bypassing bme280.c's own
        // failure-counting/bus-recovery logic since that only runs once a
        // call actually returns) leaves this timestamp frozen instead of
        // advancing, which is exactly what health_task's stall check below
        // needs to detect it.
        g_sync_state.last_env_heartbeat = xTaskGetTickCount();

        bme280_reading_t reading = {0};
        if (bme280_read(&reading) == ESP_OK) {
            if (bme280_reading_is_plausible(&reading)) {
                g_last_good_bme280_valid = true;
                g_last_good_bme280_temp_c = reading.temperature_c;
                g_last_good_bme280_humidity_pct = reading.humidity_pct;

                int16_t temp_centi = (int16_t)lroundf(reading.temperature_c * 100.0f);
                uint16_t hum_centi = (uint16_t)lroundf(reading.humidity_pct * 100.0f);
                matter_update_aux_temperature(temp_centi);
                matter_update_aux_humidity(hum_centi);
                ESP_LOGI(TAG, "BME280: %.2f\u00b0C  %.1f%%RH  %.1f hPa",
                         reading.temperature_c, reading.humidity_pct, reading.pressure_hpa);
            } else {
                ESP_LOGW(TAG, "BME280 reading rejected as implausible (temp=%.2fC hum=%.1f%%) -- likely a corrupted I2C read, keeping last known-good value",
                         reading.temperature_c, reading.humidity_pct);
            }
        } else {
            ESP_LOGW(TAG, "BME280 read failed");
        }

        vTaskDelay(pdMS_TO_TICKS(ENV_POLL_INTERVAL_MS));
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

        uint32_t last_env_age_ms =
            (xTaskGetTickCount() - g_sync_state.last_env_heartbeat) * portTICK_PERIOD_MS;
        ESP_LOGI(TAG, "Last env_task Heartbeat: %ums ago", last_env_age_ms);

        // See ENV_STALL_RESTART_MS -- confirmed on real hardware that
        // bme280_read() can hang indefinitely in a way its own internal
        // timeouts and bus-recovery logic don't catch, silently taking
        // env_task down for hours while every other task (Thread, Matter,
        // Tuya sync) kept working fine, which is exactly why this needs its
        // own independent check rather than piggybacking on the sync_task
        // one above.
        if (last_env_age_ms > ENV_STALL_RESTART_MS) {
            ESP_LOGE(TAG, "env_task appears stalled (%ums since last loop iteration, "
                          "threshold %ums) -- restarting", last_env_age_ms, ENV_STALL_RESTART_MS);
            esp_restart();
        }

        // Additional diagnostics can be added here
    }
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

    // On-demand log access -- GET /logs, POST /logs/enable, POST /logs/disable
    // -- reachable at this device's Thread IPv6 address, no physical access
    // needed. Runs regardless of whether logging is currently armed.
    log_server_start();

    // Tuya authentication requires valid system time. Retries indefinitely
    // rather than aborting -- see wait_for_time_sync() for why.
    wait_for_time_sync();

    // Initialize Tuya client
    ESP_LOGI(TAG, "Initializing Tuya client...");
    ESP_ERROR_CHECK(tuya_client_init(
        TUYA_DEVICE_ID,
        TUYA_CLIENT_ID,
        TUYA_CLIENT_SECRET
    ));

    // Initialize optional BME280 environment sensor (temperature + humidity).
    // If absent, the aux temperature endpoint falls back to the Tuya indoor temp.
    if (bme280_init() == ESP_OK) {
        // Bumped alongside health_monitor's stack -- same reasoning: its
        // ESP_LOGI calls (BME280 readings/warnings) now route through
        // papertrail_vprintf's sendto() call chain too. Not confirmed
        // crashing in practice like health_monitor was, but it's the
        // second-tightest task in the app and shares the same exposure, so
        // this is cheap, proactive margin rather than waiting for it to
        // fail the same way.
        xTaskCreate(env_task,
                    "env_sensor",
                    4096,
                    NULL,
                    2,
                    NULL);
    } else {
        ESP_LOGW(TAG, "BME280 not detected; humidity endpoint inactive, aux temp mirrors Tuya");
    }
    
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
    
    ESP_LOGI(TAG, "\n=== MiniSplit Matter Bridge Ready ===");
    ESP_LOGI(TAG, "Status Sync Interval: %ums (fixed)", STATUS_POLL_INTERVAL_MS);
    ESP_LOGI(TAG, "Command Poll Interval: %ums", COMMAND_POLL_INTERVAL_MS);
    ESP_LOGI(TAG, "Status: Waiting for Matter commissioning...\n");
}
