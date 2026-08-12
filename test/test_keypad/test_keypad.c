#include <unity.h>
#include "../../src/keypad_logic.c"

void setUp(void) {}
void tearDown(void) {}

#define HOLD 400
#define LONG 3000
#define RESET 5000

static kp_state_t s;

static void init(void) { kp_init(&s, HOLD, LONG, RESET); }

static void test_short_press_is_tap(void)
{
    init();
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_UP, true, 1000).type);
    kp_event_t e = kp_on_change(&s, KEY_UP, false, 1200);
    TEST_ASSERT_EQUAL(KP_EVT_TAP, e.type);
    TEST_ASSERT_EQUAL(KEY_UP, e.key);
}

static void test_hold_emits_start_then_end_not_tap(void)
{
    init();
    kp_on_change(&s, KEY_DOWN, true, 1000);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, 1300).type);   /* not yet */
    kp_event_t e = kp_on_tick(&s, 1450);
    TEST_ASSERT_EQUAL(KP_EVT_HOLD_START, e.type);
    TEST_ASSERT_EQUAL(KEY_DOWN, e.key);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, 1500).type);   /* only once */
    e = kp_on_change(&s, KEY_DOWN, false, 2000);
    TEST_ASSERT_EQUAL(KP_EVT_HOLD_END, e.type);
    TEST_ASSERT_EQUAL(KEY_DOWN, e.key);
}

static void test_fn_long_press_fires_once_no_hold_events(void)
{
    init();
    kp_on_change(&s, KEY_FN, true, 0);
    kp_event_t e = kp_on_tick(&s, HOLD + 50);       /* Fn does NOT jog */
    TEST_ASSERT_EQUAL(KP_EVT_NONE, e.type);
    e = kp_on_tick(&s, LONG + 10);
    TEST_ASSERT_EQUAL(KP_EVT_FN_LONG, e.type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, LONG + 500).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_FN, false, LONG + 900).type);
}

static void test_fn_medium_press_is_still_tap(void)
{
    /* Fn has no jog role: any release before long_ms is a tap. A natural
     * ~1 s press must NOT fall into a dead zone between hold_ms and long_ms. */
    init();
    kp_on_change(&s, KEY_FN, true, 100);
    kp_on_tick(&s, 100 + HOLD + 200);                 /* past hold_ms: no event */
    kp_event_t e = kp_on_change(&s, KEY_FN, false, 100 + 1000);
    TEST_ASSERT_EQUAL(KP_EVT_TAP, e.type);
    TEST_ASSERT_EQUAL(KEY_FN, e.key);
}

static void test_fn_short_press_is_tap(void)
{
    init();
    kp_on_change(&s, KEY_FN, true, 100);
    kp_event_t e = kp_on_change(&s, KEY_FN, false, 250);
    TEST_ASSERT_EQUAL(KP_EVT_TAP, e.type);
    TEST_ASSERT_EQUAL(KEY_FN, e.key);
}

/* ---- the property the whole redesign exists to guarantee ---------------- *
 *
 * A key that reads low forever must not affect any OTHER key. The classifier
 * used to carry two suppression latches whose exit condition was "these keys
 * are all released simultaneously"; a line stuck low meant that moment never
 * came, and in_reset in particular took all three keys down with it. These
 * tests fail against that design.
 */
static void test_a_key_stuck_down_forever_does_not_disable_the_others(void)
{
    init();
    kp_on_change(&s, KEY_DOWN, true, 1000);   /* sticks low and never releases */

    /* Up keeps working, indefinitely */
    for (uint32_t t = 2000; t < 600000; t += 30000) {
        kp_on_change(&s, KEY_UP, true, t);
        TEST_ASSERT_EQUAL(KP_EVT_TAP, kp_on_change(&s, KEY_UP, false, t + 100).type);
    }
    /* and so does Fn, including the calibration long-press... which is gated
     * on Up/Down being up, so check the tap at least reaches the dispatcher */
    kp_on_change(&s, KEY_FN, true, 700000);
    TEST_ASSERT_EQUAL(KP_EVT_TAP, kp_on_change(&s, KEY_FN, false, 700100).type);
}

static void test_two_keys_stuck_down_do_not_disable_the_third(void)
{
    init();
    kp_on_change(&s, KEY_UP,   true, 1000);   /* both stick */
    kp_on_change(&s, KEY_DOWN, true, 1100);

    for (uint32_t t = 5000; t < 600000; t += 60000) {
        kp_on_change(&s, KEY_FN, true, t);
        TEST_ASSERT_EQUAL(KP_EVT_TAP, kp_on_change(&s, KEY_FN, false, t + 100).type);
    }
}

/* A release must never be swallowed on account of another key's state — it is
 * the only thing that stops a jog, so swallowing one leaves the motor running. */
static void test_a_release_is_never_swallowed_by_another_key(void)
{
    init();
    kp_on_change(&s, KEY_UP, true, 0);
    TEST_ASSERT_EQUAL(KP_EVT_HOLD_START, kp_on_tick(&s, HOLD + 10).type);
    kp_on_change(&s, KEY_DOWN, true, HOLD + 100);   /* second key joins */
    kp_on_change(&s, KEY_FN,   true, HOLD + 200);   /* and a third */

    kp_event_t e = kp_on_change(&s, KEY_UP, false, HOLD + 300);
    TEST_ASSERT_EQUAL(KP_EVT_HOLD_END, e.type);     /* the jog still stops */
    TEST_ASSERT_EQUAL(KEY_UP, e.key);
}

/* Two keys pressed together are now simply two keys. The old chord latch
 * swallowed both to guard against fat-fingering; that guard is what a stuck
 * line turned into a dead keypad, and a stray tap is the price of not having
 * it. A tap is recoverable; a dead keypad is not. */
static void test_two_keys_pressed_together_each_report_normally(void)
{
    init();
    kp_on_change(&s, KEY_UP, true, 0);
    kp_on_change(&s, KEY_DOWN, true, 50);
    TEST_ASSERT_EQUAL(KP_EVT_TAP, kp_on_change(&s, KEY_UP, false, 150).type);
    TEST_ASSERT_EQUAL(KP_EVT_TAP, kp_on_change(&s, KEY_DOWN, false, 200).type);
}

/* Holding Up+Down for long_ms used to fire CHORD_REVERSE and wipe the
 * calibration. The gesture is retired; motor_reversed is a z2m setting. */
static void test_holding_up_and_down_no_longer_reverses_the_motor(void)
{
    init();
    kp_on_change(&s, KEY_UP, true, 0);
    kp_on_change(&s, KEY_DOWN, true, 100);
    for (uint32_t t = 200; t < 4 * LONG; t += 100) {
        TEST_ASSERT_NOT_EQUAL(KP_EVT_CHORD_REVERSE, kp_on_tick(&s, t).type);
    }
}

static void test_all_three_held_suppresses_fn_long_and_jog(void)
{
    /* Holding all three keys is the factory-reset gesture, so the calibration
     * long-press must not fire on the way past long_ms, and the blind must not
     * jog underneath it. Both are guards on the current down[] state — they
     * stop applying the instant any key is released. */
    init();
    kp_on_change(&s, KEY_FN, true, 0);
    kp_on_change(&s, KEY_UP, true, 100);
    kp_on_change(&s, KEY_DOWN, true, 200);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, LONG + 10).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, 200 + LONG + 10).type);

    /* release one and the others resume immediately — no latch to clear */
    kp_on_change(&s, KEY_FN, false, 200 + LONG + 20);
    TEST_ASSERT_EQUAL(KP_EVT_HOLD_START, kp_on_tick(&s, 200 + LONG + 30).type);
}

static void test_all_three_held_emits_factory_reset_once(void)
{
    init();
    kp_on_change(&s, KEY_FN, true, 0);
    kp_on_change(&s, KEY_UP, true, 100);
    kp_on_change(&s, KEY_DOWN, true, 200);          /* third press at 200 */
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, 200 + RESET - 10).type);
    TEST_ASSERT_EQUAL(KP_EVT_FACTORY_RESET, kp_on_tick(&s, 200 + RESET + 10).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, 200 + RESET + 500).type);
}

/* The accepted cost of having no suppression latch, stated explicitly so it is
 * not mistaken for a regression: abandoning the three-key gesture lets the
 * individual keys report. Up and Down were held past hold_ms without a tick,
 * so they emit nothing; Fn has no jog role, so its release is a tap. The
 * dispatcher treats a tap during motion as a stop, and taps are inert while
 * uncalibrated — so the worst case is one unasked-for travel on a calibrated
 * blind. That is recoverable. A keypad that needs a power cycle is not. */
static void test_abandoning_the_trio_emits_a_stray_tap_from_fn(void)
{
    init();
    kp_on_change(&s, KEY_FN, true, 0);
    kp_on_change(&s, KEY_UP, true, 100);
    kp_on_change(&s, KEY_DOWN, true, 200);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_DOWN, false, 3000).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_UP, false, 3050).type);
    TEST_ASSERT_EQUAL(KP_EVT_TAP, kp_on_change(&s, KEY_FN, false, 3100).type);
}

static void test_fn_long_suppressed_while_up_held(void)
{
    init();
    kp_on_change(&s, KEY_UP, true, 0);
    kp_on_change(&s, KEY_FN, true, 100);
    TEST_ASSERT_EQUAL(KP_EVT_HOLD_START, kp_on_tick(&s, 500).type);   /* Up jogs */
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, 100 + LONG + 10).type);
}

static void test_reset_gesture_rearms_after_full_release(void)
{
    init();
    kp_on_change(&s, KEY_FN, true, 0);
    kp_on_change(&s, KEY_UP, true, 100);
    kp_on_change(&s, KEY_DOWN, true, 200);
    TEST_ASSERT_EQUAL(KP_EVT_FACTORY_RESET, kp_on_tick(&s, 200 + RESET + 10).type);
    kp_on_change(&s, KEY_FN, false, 9000);
    kp_on_change(&s, KEY_UP, false, 9010);
    kp_on_change(&s, KEY_DOWN, false, 9020);
    kp_on_change(&s, KEY_FN, true, 10000);
    kp_on_change(&s, KEY_UP, true, 10010);
    kp_on_change(&s, KEY_DOWN, true, 10020);
    TEST_ASSERT_EQUAL(KP_EVT_FACTORY_RESET,
                      kp_on_tick(&s, 10020 + RESET + 10).type);
}

static void test_reset_timer_restarts_when_trio_recompletes(void)
{
    /* Releasing one key and re-pressing it must restart the 5 s window, not
     * inherit the original timestamp — otherwise the gesture fires instantly. */
    init();
    kp_on_change(&s, KEY_FN, true, 0);
    kp_on_change(&s, KEY_UP, true, 100);
    kp_on_change(&s, KEY_DOWN, true, 200);
    kp_on_change(&s, KEY_UP, false, 4000);          /* still latched */
    kp_on_change(&s, KEY_UP, true, 4100);           /* trio re-completes */
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, 4100 + RESET - 10).type);
    TEST_ASSERT_EQUAL(KP_EVT_FACTORY_RESET, kp_on_tick(&s, 4100 + RESET + 10).type);
}

/* ---- the field failures, inverted into regression guards ---------------- *
 *
 * These two scenarios used to document broken behaviour: a three-key gesture
 * cost the operator a click, and a single missed release killed the keypad
 * until reboot. Both were the in_reset latch. It is gone, and the same
 * stimulus must now behave.
 */
static void test_no_click_is_lost_after_a_three_key_gesture(void)
{
    init();
    kp_on_change(&s, KEY_UP,   true, 1000);
    kp_on_change(&s, KEY_DOWN, true, 1010);
    kp_on_change(&s, KEY_FN,   true, 1020);
    kp_on_change(&s, KEY_UP,   false, 1100);
    kp_on_change(&s, KEY_DOWN, false, 1110);
    kp_on_change(&s, KEY_FN,   false, 1120);

    /* The very next press works. It used to be swallowed, so the operator had
     * to press twice — reported from the bench as "the function key seems to
     * be effective only second click". */
    kp_on_change(&s, KEY_FN, true, 2000);
    TEST_ASSERT_EQUAL(KP_EVT_TAP, kp_on_change(&s, KEY_FN, false, 2100).type);
}

/* The field failure: one release goes missing — swallowed by the filter, or
 * the line simply never comes back up — and down[] never returns to all-false.
 * in_reset then never cleared and every key stayed dead until a power cycle.
 * A blind updated to v2.2.0 was left inoperable by exactly this. */
static void test_a_missed_release_no_longer_disables_the_keypad(void)
{
    init();
    kp_on_change(&s, KEY_UP,   true, 1000);
    kp_on_change(&s, KEY_DOWN, true, 1010);
    kp_on_change(&s, KEY_FN,   true, 1020);
    kp_on_change(&s, KEY_UP,   false, 1100);
    kp_on_change(&s, KEY_FN,   false, 1120);
    /* KEY_DOWN's release never arrives, and never will */

    for (uint32_t t = 2000; t < 600000; t += 20000) {
        kp_on_change(&s, KEY_FN, true, t);
        TEST_ASSERT_EQUAL(KP_EVT_TAP, kp_on_change(&s, KEY_FN, false, t + 100).type);
        kp_on_change(&s, KEY_UP, true, t + 200);
        TEST_ASSERT_EQUAL(KP_EVT_TAP, kp_on_change(&s, KEY_UP, false, t + 300).type);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_short_press_is_tap);
    RUN_TEST(test_hold_emits_start_then_end_not_tap);
    RUN_TEST(test_fn_long_press_fires_once_no_hold_events);
    RUN_TEST(test_fn_short_press_is_tap);
    RUN_TEST(test_fn_medium_press_is_still_tap);
    RUN_TEST(test_a_key_stuck_down_forever_does_not_disable_the_others);
    RUN_TEST(test_two_keys_stuck_down_do_not_disable_the_third);
    RUN_TEST(test_a_release_is_never_swallowed_by_another_key);
    RUN_TEST(test_two_keys_pressed_together_each_report_normally);
    RUN_TEST(test_holding_up_and_down_no_longer_reverses_the_motor);
    RUN_TEST(test_all_three_held_suppresses_fn_long_and_jog);
    RUN_TEST(test_all_three_held_emits_factory_reset_once);
    RUN_TEST(test_abandoning_the_trio_emits_a_stray_tap_from_fn);
    RUN_TEST(test_fn_long_suppressed_while_up_held);
    RUN_TEST(test_reset_gesture_rearms_after_full_release);
    RUN_TEST(test_reset_timer_restarts_when_trio_recompletes);
    RUN_TEST(test_no_click_is_lost_after_a_three_key_gesture);
    RUN_TEST(test_a_missed_release_no_longer_disables_the_keypad);
    return UNITY_END();
}
