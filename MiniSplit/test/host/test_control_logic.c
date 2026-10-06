#include <stdio.h>
#include <string.h>
#include "control_logic.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        g_checks++;                                                              \
        if (!(cond)) {                                                           \
            g_failures++;                                                        \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                        \
    } while (0)

static tuya_device_status_t status_on(uint8_t ac_mode, int16_t temp_set_f)
{
    tuya_device_status_t s;
    memset(&s, 0, sizeof(s));
    s.switch_state = true;
    s.ac_mode = ac_mode;
    s.temp_set_f = temp_set_f;
    s.light = true;
    s.fan_speed = 0;
    return s;
}

static void build(const tuya_device_status_t *s, bool power_on, bool ov_mode, uint8_t ir_mode,
                  bool ov_sp, int16_t sp_c_x100, uint8_t frame[IR_TCL112_FRAME_LEN])
{
    uint8_t out_mode;
    int16_t out_sp;
    build_ir_state_frame(s, power_on, ov_mode, ir_mode, ov_sp, sp_c_x100, frame, &out_mode, &out_sp);
}

static void test_mode_mapping(void)
{
    tuya_device_status_t s = status_on(4, 69);
    CHECK(map_tuya_mode_to_matter(&s) == 4);  // heat
    s.ac_mode = 1;
    CHECK(map_tuya_mode_to_matter(&s) == 3);  // cool
    s.ac_mode = 0;
    CHECK(map_tuya_mode_to_matter(&s) == 1);  // auto
    s.switch_state = false;
    CHECK(map_tuya_mode_to_matter(&s) == 0);  // off wins over any mode

    CHECK(map_matter_mode_to_ir(4) == IR_MODE_HEAT);
    CHECK(map_matter_mode_to_ir(3) == IR_MODE_COOL);
    CHECK(map_matter_mode_to_ir(0) == -1);    // off has no IR mode
    CHECK(map_matter_mode_to_ir(5) == -1);    // emergency heat: unmappable
    CHECK(map_matter_mode_to_tuya(4) == 4);
    CHECK(map_matter_mode_to_tuya(3) == 1);
}

static void test_fan_mapping(void)
{
    CHECK(map_tuya_fan_speed_to_ir(0) == IR_FAN_AUTO);
    CHECK(map_tuya_fan_speed_to_ir(1) == IR_FAN_LOW);   // mute
    CHECK(map_tuya_fan_speed_to_ir(3) == IR_FAN_LOW);   // med-low
    CHECK(map_tuya_fan_speed_to_ir(5) == IR_FAN_MED);   // med-high
    CHECK(map_tuya_fan_speed_to_ir(7) == IR_FAN_HIGH);  // turbo
}

static void test_frame_power_light_fan(void)
{
    uint8_t f[IR_TCL112_FRAME_LEN];
    tuya_device_status_t s = status_on(4, 68);
    s.fan_speed = 7;

    build(&s, true, false, 0, false, 0, f);
    CHECK(f[5] & 0x04);                         // power on
    CHECK(!(f[5] & 0x40));                      // light on = bit clear (inverted)
    CHECK((f[8] & 0x07) == IR_FAN_HIGH);
    CHECK((f[6] & 0x0F) == IR_MODE_HEAT);

    build(&s, false, false, 0, false, 0, f);
    CHECK(!(f[5] & 0x04));                      // power off

    s.light = false;
    build(&s, true, false, 0, false, 0, f);
    CHECK(f[5] & 0x40);                         // light off = bit set

    // With the unit off, the mode byte falls back to Auto -- the reason
    // every non-power IR path must not send while the unit is off.
    s.switch_state = false;
    build(&s, true, false, 0, false, 0, f);
    CHECK((f[6] & 0x0F) == IR_MODE_AUTO);
}

static void test_frame_setpoint(void)
{
    uint8_t f[IR_TCL112_FRAME_LEN];
    tuya_device_status_t s = status_on(4, 68);  // 20.0C
    build(&s, true, false, 0, false, 0, f);
    CHECK(f[7] == 11);                          // Temp = 31 - 20
    CHECK(!(f[12] & 0x04));                     // no half degree
    CHECK(!(f[12] & 0x20));                     // 0x20 never set (unit ignores it)

    s.temp_set_f = 69;                          // 20.56C -> 20.5C
    build(&s, true, false, 0, false, 0, f);
    CHECK(f[7] == 11);
    CHECK(f[12] & 0x04);                        // half degree
    CHECK(!(f[12] & 0x20));

    // Matches the real remote's captured frames (Heat): 70F = 21C + 0x80,
    // 71F = 21C + 0x84.
    build(&s, true, true, IR_MODE_HEAT, true, 2111, f);
    CHECK(f[7] == 10 && f[12] == 0x80);
    build(&s, true, true, IR_MODE_HEAT, true, 2167, f);
    CHECK(f[7] == 10 && f[12] == 0x84);

    // Overrides snap to the whole F they round to before stepping, so the
    // unit lands on tuya_setpoint_c_to_f(override) exactly.
    build(&s, true, false, 0, true, 2400, f);   // 24.00C = 75.2F -> 75F -> 24.0C
    CHECK(f[7] == 7);
    CHECK(!(f[12] & 0x04));
    build(&s, true, false, 0, true, 2125, f);   // 21.25C = 70.25F -> 70F -> 21.0C, not 21.5C
    CHECK(f[7] == 10 && !(f[12] & 0x04));
    // Every whole F: the step sent reads back (Tuya C->F) as that same F.
    for (int16_t want = 61; want <= 86; want++) {
        build(&s, true, false, 0, true, tuya_setpoint_f_to_c(want), f);
        int16_t sent_x100 = (int16_t)((31 - f[7]) * 100 + ((f[12] & 0x04) ? 50 : 0));
        CHECK(tuya_setpoint_c_to_f(sent_x100) == want);
    }

    build(&s, true, false, 0, true, 1000, f);   // below 16C clamps to 16
    CHECK(f[7] == 15);

    build(&s, true, true, IR_MODE_COOL, false, 0, f);
    CHECK((f[6] & 0x0F) == IR_MODE_COOL);       // mode override
}

static void test_setpoint_f_round_trip(void)
{
    // Every whole-F setpoint survives F -> C x100 -> F (ceiling broke 73F).
    for (int16_t f = 61; f <= 86; f++) {
        CHECK(tuya_setpoint_c_to_f(tuya_setpoint_f_to_c(f)) == f);
    }
    // Tuya's reported F for the unit's raw C (live, 2026-10-05).
    CHECK(tuya_setpoint_c_to_f(2050) == 69);
    CHECK(tuya_setpoint_c_to_f(2100) == 70);
    CHECK(tuya_setpoint_c_to_f(2150) == 71);
    CHECK(tuya_setpoint_c_to_f(2200) == 72);
}

static void test_setpoint_reconcile(void)
{
    int16_t pending = SETPOINT_UNKNOWN_F;
    setpoint_reconcile_input_t in = {
        .desired_f = 70, .unit_f = 70, .prev_unit_f = 70, .correctable = true,
        .mode_changed = false, .sent_recently = false, .last_sent_f = 70,
    };
    CHECK(setpoint_reconcile_decide(&in, &pending) == SETPOINT_RECONCILE_IN_SYNC);

    // Missed update: HA moved Desired to 68, the bridge sent it, the unit
    // stayed at 70 -> resend, never adopt.
    in.desired_f = 68; in.sent_recently = true; in.last_sent_f = 68;
    CHECK(setpoint_reconcile_decide(&in, &pending) == SETPOINT_RECONCILE_RESEND);
    CHECK(setpoint_reconcile_decide(&in, &pending) == SETPOINT_RECONCILE_RESEND);

    // Our own send landing is in sync, not a manual change.
    in.unit_f = 68; in.prev_unit_f = 70;
    CHECK(setpoint_reconcile_decide(&in, &pending) == SETPOINT_RECONCILE_IN_SYNC);

    // Remote press 68 -> 72: wait one poll, then adopt.
    in.unit_f = 72; in.prev_unit_f = 68; in.sent_recently = false;
    CHECK(setpoint_reconcile_decide(&in, &pending) == SETPOINT_RECONCILE_WAIT);
    CHECK(pending == 72);
    in.prev_unit_f = 72;
    CHECK(setpoint_reconcile_decide(&in, &pending) == SETPOINT_RECONCILE_ADOPT);
    CHECK(pending == SETPOINT_UNKNOWN_F);

    // ...even right after a bridge send, if it's not the value we sent.
    in.desired_f = 68; in.unit_f = 71; in.prev_unit_f = 68; in.sent_recently = true; in.last_sent_f = 68;
    CHECK(setpoint_reconcile_decide(&in, &pending) == SETPOINT_RECONCILE_WAIT);

    // A one-poll glitch that goes back is never adopted.
    in.unit_f = 68; in.prev_unit_f = 71;
    CHECK(setpoint_reconcile_decide(&in, &pending) == SETPOINT_RECONCILE_IN_SYNC);
    CHECK(pending == SETPOINT_UNKNOWN_F);

    // Remote MODE press (carries the remote's temp) -> resend Desired.
    in.unit_f = 75; in.prev_unit_f = 68; in.mode_changed = true; in.sent_recently = false;
    CHECK(setpoint_reconcile_decide(&in, &pending) == SETPOINT_RECONCILE_RESEND);
    in.mode_changed = false;

    // No previous poll (boot): resend, don't adopt.
    in.prev_unit_f = SETPOINT_UNKNOWN_F;
    CHECK(setpoint_reconcile_decide(&in, &pending) == SETPOINT_RECONCILE_RESEND);

    // Off or Auto: hands off.
    in.correctable = false;
    CHECK(setpoint_reconcile_decide(&in, &pending) == SETPOINT_RECONCILE_SKIP);
}

static void test_followme_bits(void)
{
    uint8_t f[IR_TCL112_FRAME_LEN];
    tuya_device_status_t s = status_on(4, 68);
    build(&s, true, false, 0, false, 0, f);
    CHECK(f[5] & 0x20);                         // base template has 0x20 set
    CHECK(!(f[4] & 0x80) && !(f[6] & 0x80));    // plain frame = Follow-Me off

    ir_frame_set_followme(f, 21);
    CHECK(f[4] & 0x80);
    CHECK(f[6] & 0x80);
    CHECK(!(f[5] & 0x20));                      // heartbeat, never enable
    CHECK(f[11] == 21);
    CHECK(f[5] & 0x04);                         // power untouched
    CHECK((f[6] & 0x0F) == IR_MODE_HEAT);       // mode untouched
}

static void test_fresh_air(void)
{
    CHECK(fresh_air_effective(true, true, 0, false) == true);
    CHECK(fresh_air_effective(true, true, FRESH_AIR_CONFIRM_WINDOW_MS - 1, false) == true);
    CHECK(fresh_air_effective(true, true, FRESH_AIR_CONFIRM_WINDOW_MS, false) == false);
    CHECK(fresh_air_effective(false, true, 0, false) == false);
    CHECK(fresh_air_effective(true, false, 1000, true) == false);
}

static mode_reconcile_input_t mismatch(uint8_t actual, uint8_t desired)
{
    mode_reconcile_input_t in = {.actual = actual, .desired = desired};
    return in;
}

static void test_mode_reconcile(void)
{
    uint8_t polls;
    mode_reconcile_input_t in;

    polls = 3;
    in = mismatch(4, MATTER_MODE_UNKNOWN);
    CHECK(mode_reconcile_decide(&in, &polls) == MODE_RECONCILE_ADOPT_UNKNOWN && polls == 0);

    polls = 1;
    in = mismatch(4, 4);
    CHECK(mode_reconcile_decide(&in, &polls) == MODE_RECONCILE_IN_SYNC && polls == 0);

    polls = 1;
    in = mismatch(0, 4);
    in.command_pending = true;
    CHECK(mode_reconcile_decide(&in, &polls) == MODE_RECONCILE_IN_SYNC && polls == 0);

    // First mismatched poll only waits.
    polls = 0;
    in = mismatch(1, 4);
    in.unit_moved_last = true;
    CHECK(mode_reconcile_decide(&in, &polls) == MODE_RECONCILE_WAIT_POLLS && polls == 1);

    // Remote/app switched the unit to auto: Desired adopts it.
    CHECK(mode_reconcile_decide(&in, &polls) == MODE_RECONCILE_ADOPT && polls == 0);

    // 2026-10-01: unit reported "off" ~15 min after our own Off command,
    // after Desired had moved back to heat -- a late echo, so resend heat.
    polls = 1;
    in = mismatch(0, 4);
    in.unit_moved_last = true;
    in.actual_recently_commanded = true;
    CHECK(mode_reconcile_decide(&in, &polls) == MODE_RECONCILE_RESEND);

    // A command that didn't land (Desired moved last): resend.
    polls = 1;
    in = mismatch(0, 4);
    CHECK(mode_reconcile_decide(&in, &polls) == MODE_RECONCILE_RESEND && polls == 2);

    // ...but not again while the last send may still be landing.
    polls = 1;
    in = mismatch(0, 4);
    in.desired_recently_commanded = true;
    CHECK(mode_reconcile_decide(&in, &polls) == MODE_RECONCILE_WAIT_ECHO);

    // Desired Off is resendable (a power-off), not "unmappable".
    polls = 1;
    in = mismatch(4, 0);
    CHECK(mode_reconcile_decide(&in, &polls) == MODE_RECONCILE_RESEND);

    // A mode with no IR encoding gets dropped in favor of the unit's.
    polls = 1;
    in = mismatch(4, 5);
    CHECK(mode_reconcile_decide(&in, &polls) == MODE_RECONCILE_ADOPT_UNMAPPABLE && polls == 0);
}

int main(void)
{
    test_mode_mapping();
    test_fan_mapping();
    test_frame_power_light_fan();
    test_frame_setpoint();
    test_setpoint_f_round_trip();
    test_setpoint_reconcile();
    test_followme_bits();
    test_fresh_air();
    test_mode_reconcile();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
