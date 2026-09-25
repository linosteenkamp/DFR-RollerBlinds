#include <unity.h>
#include "../../src/position.c"
#include "../../src/ramp.c"
#include "../../src/ctl.c"
#include "fake_ports.h"

void setUp(void) {}
void tearDown(void) {}

#define SPAN 24000

static ctl_t C;
static const ctl_config_t CFG = {
    .move = { 150, 500, 800 },
    .jog  = { 300, 900, 800 },
    .min_span_steps  = 6000,
    .hard_cap_margin = 2400,
    .jog_unbounded   = 2000000,
};

/* Boot from a given NVS state; clears the call log afterwards so each test
 * sees only what its own events caused. */
static void boot_full(bool span_ok, int32_t span, bool pos_ok, int32_t pos,
                      bool moving_flag, uint16_t travel)
{
    fake_reset();
    blind_store_data_t b = {
        .span_valid = span_ok, .closed_steps = span,
        .pos_known = pos_ok, .cur_steps = pos,
        .motor_reversed = false, .move_in_progress = moving_flag,
        .travel_secs = travel,
    };
    ctl_init(&C, &FAKE_PORTS, &CFG, &b);
    F.n = 0;
}
static void boot_cal(int32_t pos) { boot_full(true, SPAN, true, pos, false, 0); }
static void boot_uncal(void)      { boot_full(false, 0, false, 0, false, 0); }

static void ev_type(app_event_type_t t)
{
    app_event_t e = { .type = t };
    ctl_handle(&C, &e);
}
static void kp(kp_event_type_t t, key_id_t k)
{
    app_event_t e = { .type = APP_EVT_KEYPAD, .kp = { .type = t, .key = k } };
    ctl_handle(&C, &e);
}
static void zb_goto(uint8_t pct)
{
    app_event_t e = { .type = APP_EVT_ZB_GOTO, .pct = pct };
    ctl_handle(&C, &e);
}
static void ev_on(app_event_type_t t, bool on)
{
    app_event_t e = { .type = t, .on = on };
    ctl_handle(&C, &e);
}
static void ev_secs(uint16_t secs)
{
    app_event_t e = { .type = APP_EVT_ZB_SET_SPEED, .secs = secs };
    ctl_handle(&C, &e);
}
/* The ISR finished and its MOTION_DONE is delivered now. */
static void done(int32_t steps, bool completed)
{
    F.moving = false;
    app_event_t e = { .type = APP_EVT_MOTION_DONE, .steps = steps,
                      .completed = completed };
    ctl_handle(&C, &e);
}

/* ---------- boot ---------- */

static void test_boot_clean_calibrated_restores_without_saving(void)
{
    boot_full(true, SPAN, true, 12000, false, 0);
    TEST_ASSERT_TRUE(ctl_calibrated(&C));
    TEST_ASSERT_EQUAL_INT32(12000, C.raw);
    fake_reset();
    blind_store_data_t b = { .span_valid = true, .closed_steps = SPAN,
                             .pos_known = true, .cur_steps = 12000 };
    ctl_init(&C, &FAKE_PORTS, &CFG, &b);
    TEST_ASSERT_EQUAL_INT(0, f_count(F_SAVE_POSITION));
    TEST_ASSERT_EQUAL_INT(0, f_count(F_SAVE_MOVE_FLAG));
}

static void test_boot_with_move_flag_drops_to_position_unknown(void)
{
    fake_reset();
    blind_store_data_t b = { .span_valid = true, .closed_steps = SPAN,
                             .pos_known = true, .cur_steps = 5000,
                             .move_in_progress = true };
    ctl_init(&C, &FAKE_PORTS, &CFG, &b);
    TEST_ASSERT_FALSE(ctl_calibrated(&C));
    TEST_ASSERT_FALSE(C.pos.pos_known);
    TEST_ASSERT_TRUE(C.pos.span_valid);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_SAVE_POSITION, false, 0));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_SAVE_MOVE_FLAG, false));
    TEST_ASSERT_EQUAL_INT32(0, C.raw);
}

static void test_boot_travel_time_sets_cruise(void)
{
    boot_full(true, 300000, true, 0, false, 30);   /* 30 s over 300 000 steps */
    TEST_ASSERT_EQUAL_UINT32(100, C.cfg.move.cruise_us);
    boot_full(true, 300000, true, 0, false, 0);    /* never set -> default */
    TEST_ASSERT_EQUAL_UINT32(RAMP_DEFAULT_CRUISE_US, C.cfg.move.cruise_us);
}

/* ---------- LED base pattern ---------- */

static void test_led_pattern_priority_ladder(void)
{
    boot_cal(0);
    TEST_ASSERT_EQUAL(LED_OFF, ctl_led_pattern(&C));
    F.joined = false;
    TEST_ASSERT_EQUAL(LED_NO_NETWORK, ctl_led_pattern(&C));
    boot_uncal();
    F.joined = false;
    TEST_ASSERT_EQUAL(LED_NO_NETWORK, ctl_led_pattern(&C));  /* not UNCAL */
    F.joined = true;
    TEST_ASSERT_EQUAL(LED_UNCAL, ctl_led_pattern(&C));
    C.pos.cal = POS_CAL_WAIT_MARK2;
    TEST_ASSERT_EQUAL(LED_CAL_MARK2, ctl_led_pattern(&C));
    C.identifying = true;
    TEST_ASSERT_EQUAL(LED_IDENTIFY, ctl_led_pattern(&C));
}

/* ---------- keypad travel ---------- */

static void test_tap_down_calibrated_runs_full_travel(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_MOTION_START, 0, SPAN));
    TEST_ASSERT_TRUE(f_first(F_SAVE_MOVE_FLAG) < f_first(F_MOTION_START));
    TEST_ASSERT_EQUAL_INT32(1, f_rec(F_SAVE_MOVE_FLAG).a);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_REPORT_TIMER_START));
    TEST_ASSERT_EQUAL_INT32(SPAN + 2400, F.last_cap);
    TEST_ASSERT_EQUAL_UINT32(150, F.last_cruise);
}

static void test_tap_at_limit_acks_without_moving(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_UP);
    TEST_ASSERT_EQUAL_INT(0, f_count(F_MOTION_START));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_LED_FLASH, LED_ACK));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_TRACE, TRC_MOVE_NOOP));
}

static void test_tap_while_moving_stops(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    kp(KP_EVT_TAP, KEY_UP);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_START));
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_STOP));
}

static void test_tap_uncalibrated_flashes_error(void)
{
    boot_uncal();
    kp(KP_EVT_TAP, KEY_DOWN);
    TEST_ASSERT_EQUAL_INT(0, f_count(F_MOTION_START));
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_LED_FLASH, LED_ERROR));
}

static void test_motion_done_persists_then_clears_flag(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    F.n = 0;
    done(SPAN, true);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_SAVE_POSITION, true, SPAN));
    TEST_ASSERT_TRUE(f_first(F_SAVE_POSITION) < f_first(F_SAVE_MOVE_FLAG));
    TEST_ASSERT_EQUAL_INT32(0, f_rec(F_SAVE_MOVE_FLAG).a);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_REPORT_TIMER_STOP));
    TEST_ASSERT_EQUAL_INT32(100, f_rec(F_REPORT_LIFT).a);
    TEST_ASSERT_EQUAL_INT32(SPAN, C.raw);
}

static void test_position_save_failure_leaves_move_flag_set(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    F.n = 0;
    F.fail[F_SAVE_POSITION] = 0x105;
    done(SPAN, true);
    TEST_ASSERT_EQUAL_INT(0, f_count(F_SAVE_MOVE_FLAG));
}

static void test_dead_zone_move_acks(void)
{
    boot_cal(12000);                       /* 50 % */
    kp(KP_EVT_HOLD_START, KEY_DOWN);
    kp(KP_EVT_HOLD_END, KEY_DOWN);
    F.n = 0;
    done(12050, false);                    /* still 50 % */
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_LED_FLASH, LED_ACK));
}

static void test_refused_move_flashes_error_and_clears_flag(void)
{
    boot_cal(0);
    F.start_err = 0x103;
    kp(KP_EVT_TAP, KEY_DOWN);
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_SAVE_MOVE_FLAG, true));
    TEST_ASSERT_EQUAL_INT32(0, f_rec(F_SAVE_MOVE_FLAG).a);
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_LED_FLASH, LED_ERROR));
    TEST_ASSERT_EQUAL_INT(0, f_count(F_REPORT_TIMER_START));
}

static void test_uncalibrated_jog_is_unbounded_and_uncapped(void)
{
    boot_uncal();
    kp(KP_EVT_HOLD_START, KEY_UP);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_MOTION_START, 0, -2000000));
    TEST_ASSERT_EQUAL_INT32(2000001, F.last_cap);
    TEST_ASSERT_EQUAL_UINT32(300, F.last_cruise);
}

static void test_calibrated_jog_is_clamped_to_limits(void)
{
    boot_cal(12000);
    kp(KP_EVT_HOLD_START, KEY_UP);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_MOTION_START, 12000, 0));
}

/* ---------- Zigbee ---------- */

static void test_zb_goto_idle_moves(void)
{
    boot_cal(0);
    zb_goto(50);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_MOTION_START, 0, 12000));
}

static void test_zb_goto_uncalibrated_is_ignored(void)
{
    boot_uncal();
    zb_goto(50);
    TEST_ASSERT_EQUAL_INT(0, f_count(F_MOTION_START));
}

static void test_zb_goto_while_moving_parks_and_runs_after_done(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    zb_goto(25);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_STOP));
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_START));
    done(8000, false);
    TEST_ASSERT_EQUAL_INT(1, f_count_ab(F_MOTION_START, 8000, 6000));
}

static void test_zb_stop_discards_parked_goto(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    zb_goto(25);
    ev_type(APP_EVT_ZB_STOP);
    done(8000, false);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_START));
}

static void test_keypad_input_discards_parked_goto(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    zb_goto(25);
    kp(KP_EVT_TAP, KEY_UP);          /* local input is the last writer */
    done(8000, false);
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_START));
}

static void test_report_tick_reports_live_lift_while_moving(void)
{
    boot_cal(0);
    kp(KP_EVT_TAP, KEY_DOWN);
    F.steps = 6000;
    F.n = 0;
    ev_type(APP_EVT_REPORT_TICK);
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_REPORT_LIFT, 25));
}

static void test_travel_time_write_persists_and_reports_applied(void)
{
    boot_full(true, 300000, true, 0, false, 0);
    ev_secs(30);
    TEST_ASSERT_EQUAL_INT(1, f_count_a(F_SAVE_TRAVEL, 30));
    TEST_ASSERT_EQUAL_UINT32(100, C.cfg.move.cruise_us);
    TEST_ASSERT_EQUAL_INT32(30, f_rec(F_REPORT_TRAVEL).a);
}

/* Review Focus 4: a travel-time write mid-move applies to the NEXT move. */
static void test_travel_time_write_mid_move_applies_to_next_move(void)
{
    boot_full(true, 300000, true, 0, false, 30);    /* cruise 100 */
    kp(KP_EVT_TAP, KEY_DOWN);
    TEST_ASSERT_EQUAL_UINT32(100, F.last_cruise);
    ev_secs(60);                                     /* cruise 200 from now on */
    TEST_ASSERT_EQUAL_INT(1, f_count(F_MOTION_START));   /* running move untouched */
    done(300000, true);
    kp(KP_EVT_TAP, KEY_UP);
    TEST_ASSERT_EQUAL_UINT32(200, F.last_cruise);
}

static void test_identify_overrides_then_restores_led(void)
{
    boot_cal(0);
    ev_on(APP_EVT_IDENTIFY, true);
    TEST_ASSERT_EQUAL_INT32(LED_IDENTIFY, f_rec(F_LED_SET).a);
    ev_on(APP_EVT_IDENTIFY, false);
    TEST_ASSERT_EQUAL_INT32(LED_OFF, f_rec(F_LED_SET).a);
}

static void test_network_lost_shows_no_network(void)
{
    boot_cal(0);
    F.joined = false;
    ev_type(APP_EVT_ZB_NET_LOST);
    TEST_ASSERT_EQUAL_INT32(LED_NO_NETWORK, f_rec(F_LED_SET).a);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_boot_clean_calibrated_restores_without_saving);
    RUN_TEST(test_boot_with_move_flag_drops_to_position_unknown);
    RUN_TEST(test_boot_travel_time_sets_cruise);
    RUN_TEST(test_led_pattern_priority_ladder);
    RUN_TEST(test_tap_down_calibrated_runs_full_travel);
    RUN_TEST(test_tap_at_limit_acks_without_moving);
    RUN_TEST(test_tap_while_moving_stops);
    RUN_TEST(test_tap_uncalibrated_flashes_error);
    RUN_TEST(test_motion_done_persists_then_clears_flag);
    RUN_TEST(test_position_save_failure_leaves_move_flag_set);
    RUN_TEST(test_dead_zone_move_acks);
    RUN_TEST(test_refused_move_flashes_error_and_clears_flag);
    RUN_TEST(test_uncalibrated_jog_is_unbounded_and_uncapped);
    RUN_TEST(test_calibrated_jog_is_clamped_to_limits);
    RUN_TEST(test_zb_goto_idle_moves);
    RUN_TEST(test_zb_goto_uncalibrated_is_ignored);
    RUN_TEST(test_zb_goto_while_moving_parks_and_runs_after_done);
    RUN_TEST(test_zb_stop_discards_parked_goto);
    RUN_TEST(test_keypad_input_discards_parked_goto);
    RUN_TEST(test_report_tick_reports_live_lift_while_moving);
    RUN_TEST(test_travel_time_write_persists_and_reports_applied);
    RUN_TEST(test_travel_time_write_mid_move_applies_to_next_move);
    RUN_TEST(test_identify_overrides_then_restores_led);
    RUN_TEST(test_network_lost_shows_no_network);
    return UNITY_END();
}
