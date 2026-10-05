#include "control_logic.h"
#include <string.h>

// See control_logic.h. Moved verbatim from main.c (2026-10-05) apart from
// dropping `static`; the extra helpers at the bottom are new.

// Tuya "mode" DP: 0=auto, 1=cool, 2=dry, 3=fan, 4=heat.
// Matter Thermostat SystemModeEnum: kOff=0, kAuto=1, kCool=3, kFanOnly=7, kDry=8, kHeat=4.
uint8_t map_tuya_mode_to_matter(const tuya_device_status_t *device_status)
{
    if (!device_status->switch_state) {
        return 0; // kOff
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
int8_t map_matter_mode_to_tuya(uint8_t matter_mode)
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
int8_t map_matter_mode_to_ir(uint8_t matter_mode)
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
uint8_t map_tuya_fan_speed_to_ir(uint8_t tuya_fan_speed)
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

// Fields this frame does NOT carry from the unit's live state (Health,
// swing, eco, turbo, quiet, timers, fan levels beyond IR's 4) are by design
// -- see IR_PROTOCOL_REFERENCE.md's "Fields not preserved across
// bridge-sent frames" table.
// Builds the frame array only (no send) -- shared by send_ir_frame() below
// and send_followme_frame() (Follow-Me heartbeat), since both need the same
// base-template-plus-known-fields construction and only differ in which
// extra bits/bytes they layer on afterward.
void build_ir_state_frame(const tuya_device_status_t *status, bool power_on,
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

    // status->temp_set (raw Celsius DP) was the no-override source here until
    // 2026-10-01 -- every other reader of Tuya's setpoint in this file
    // switched to temp_set_f-derived tuya_setpoint_f_to_c() back on
    // 2026-09-01 (see apply_status_to_matter()'s comment) after finding
    // temp_set is "coarser 0.5C-quantized... sitting a full degree off"
    // temp_set_f; this function was the one place still reading the
    // untrusted field directly. Root-caused via raw-pin capture comparison
    // against the real remote (MiniSplitIR/capture_tools/esp_capture vs
    // remote_capture): Follow-Me/mode-change frames consistently set the
    // HalfDegree bit (state[12] 0x20) that the real remote's equivalent
    // frames leave clear, for the same whole-degree Temp byte -- status->
    // temp_set's own imprecision was producing an odd half_steps count that
    // temp_set_f-derived values don't.
    int16_t setpoint_c_x100 = override_setpoint ? override_setpoint_c_x100
                                                 : tuya_setpoint_f_to_c(status->temp_set_f);
    // Round to the nearest HALF degree C, not whole degree -- 2026-09-09.
    // Whole-degree-only rounding here was never a real hardware limit: it
    // was found (live, real remote vs. Device B comparison) that sending
    // 71F from the real remote makes the unit report back exactly 71F,
    // which whole-degree Celsius cannot represent (21C=69.8F, 22C=71.6F --
    // both round to something other than 71F, the "tie" IR_PROTOCOL_REFERENCE.md
    // and the 2026-09-09 capture session attributed to AC hardware
    // resolution). IRremoteESP8266's own ir_Tcl.cpp resolves this:
    // `state[12]` bit `0x20` (HalfDegree -- "sourced, unconfirmed... not
    // used by this project" per this project's own prior notes) adds +0.5C
    // on top of the Temp field's whole-degree value (setTemp(): nrHalfDegrees
    // = round(C*2), HalfDegree = nrHalfDegrees & 1, Temp = 31 -
    // nrHalfDegrees/2). 21.5C (Temp=10, HalfDegree set) converts to exactly
    // 71F via tuya_setpoint_c_to_f(), matching the real remote's observed
    // behavior -- this was a real, fixable firmware gap, not an AC limit.
    //
    // half_steps counts 0.5C increments, rounded to nearest (round-half-up;
    // setpoints are always positive, 16-31C range enforced below via
    // half_steps' own 32-62 clamp, so the +25-before-truncate offset is
    // exact -- same reasoning as the whole-degree rounding this replaces).
    int16_t half_steps = (int16_t)((setpoint_c_x100 + 25) / 50);
    if (half_steps < 32) {        // 16.0C
        half_steps = 32;
    } else if (half_steps > 62) { // 31.0C
        half_steps = 62;
    }
    bool half_degree = (half_steps & 1) != 0;
    // Whole-degree floor (e.g. 21 for both 21.0C and 21.5C) -- state[7]'s
    // own value per setTemp()'s formula; the actual transmitted temperature
    // is this plus 0.5 whenever half_degree is set below. Logging elsewhere
    // in this function reports this floor, not the true half-degree value --
    // acceptable, logging was never precise here to begin with.
    int16_t setpoint_c = (int16_t)(31 - half_steps / 2);

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
    out_frame[7] = (uint8_t)setpoint_c;
    // HalfDegree -- state[12] bit 0x20, see half_steps' doc comment above.
    // isTcl (bit 0x80) and the anti-repeat toggle (bit 0x04) are left as
    // the base template's values; frame[13] (checksum) is recomputed fresh
    // by ir_tcl112_send().
    if (half_degree) {
        out_frame[12] |= 0x20;
    } else {
        out_frame[12] &= (uint8_t)~0x20;
    }
    //
    // Fresh Air is NOT set anywhere in this Type 1 frame -- bit-level capture
    // comparison (2026-09-10, MiniSplitIR/capture_tools' microsecond-resolution
    // RawPinTest) found Type 1's state[12] byte-for-byte identical (0x84)
    // regardless of Fresh Air state across 6 captures (3 on, 3 off). The real
    // remote instead mirrors current Fresh Air state into the Type 2
    // companion frame's state[12] bit 0x01 -- see transmit_ir_state_frame()'s
    // kType2CompanionFrame handling below, where it's now wired from
    // `status->fresh_air_valve`.

    *out_ir_mode = ir_mode;
    *out_setpoint_c = setpoint_c;
}

void ir_frame_set_followme(uint8_t frame[IR_TCL112_FRAME_LEN], int8_t ambient_c)
{
    frame[4] |= 0x80;
    frame[6] |= 0x80;
    frame[5] &= (uint8_t)~0x20;
    frame[11] = (uint8_t)ambient_c;
}

bool fresh_air_effective(bool cmd_valid, bool cmd_value, uint32_t ms_since_cmd, bool reported)
{
    if (cmd_valid && ms_since_cmd < FRESH_AIR_CONFIRM_WINDOW_MS) {
        return cmd_value;
    }
    return reported;
}

mode_reconcile_action_t mode_reconcile_decide(const mode_reconcile_input_t *in, uint8_t *mismatch_polls)
{
    if (in->desired == MATTER_MODE_UNKNOWN) {
        *mismatch_polls = 0;
        return MODE_RECONCILE_ADOPT_UNKNOWN;
    }
    if (in->actual == in->desired || in->command_pending) {
        *mismatch_polls = 0;
        return MODE_RECONCILE_IN_SYNC;
    }
    if (++(*mismatch_polls) < MODE_MISMATCH_POLLS) {
        return MODE_RECONCILE_WAIT_POLLS;
    }
    if (in->unit_moved_last && !in->actual_recently_commanded) {
        *mismatch_polls = 0;
        return MODE_RECONCILE_ADOPT;
    }
    if (in->desired != 0 && map_matter_mode_to_ir(in->desired) < 0) {
        *mismatch_polls = 0;
        return MODE_RECONCILE_ADOPT_UNMAPPABLE;
    }
    if (in->desired_recently_commanded) {
        return MODE_RECONCILE_WAIT_ECHO;
    }
    return MODE_RECONCILE_RESEND;
}
