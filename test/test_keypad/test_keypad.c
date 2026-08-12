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

static void test_chord_fires_once_and_suppresses_up_down_events(void)
{
    init();
    kp_on_change(&s, KEY_UP, true, 0);
    kp_on_change(&s, KEY_DOWN, true, 100);
    /* chord timing counts from the SECOND key press (t=100) */
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, LONG + 50).type);   /* 3050 < 3100 */
    kp_event_t e = kp_on_tick(&s, 100 + LONG + 10);
    TEST_ASSERT_EQUAL(KP_EVT_CHORD_REVERSE, e.type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, 100 + LONG + 200).type);  /* once */
    /* releases after a chord are swallowed — no TAP/HOLD_END */
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_UP, false, 100 + LONG + 300).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_DOWN, false, 100 + LONG + 400).type);
    /* next single press works normally again */
    kp_on_change(&s, KEY_UP, true, 9000);
    TEST_ASSERT_EQUAL(KP_EVT_TAP, kp_on_change(&s, KEY_UP, false, 9100).type);
}

static void test_two_keys_without_long_hold_are_independent(void)
{
    init();
    kp_on_change(&s, KEY_UP, true, 0);
    kp_on_change(&s, KEY_DOWN, true, 50);
    /* both released quickly: chord never fired, but both were suppressed as
     * a chord ATTEMPT -> no stray TAPs from a fat-finger */
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_UP, false, 150).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_DOWN, false, 200).type);
}

static void test_chord_during_jog_ends_hold(void)
{
    init();
    kp_on_change(&s, KEY_UP, true, 0);
    TEST_ASSERT_EQUAL(KP_EVT_HOLD_START, kp_on_tick(&s, HOLD + 10).type);
    kp_event_t e = kp_on_change(&s, KEY_DOWN, true, HOLD + 100);  /* chord latch */
    TEST_ASSERT_EQUAL(KP_EVT_HOLD_END, e.type);   /* jog is closed, not orphaned */
    TEST_ASSERT_EQUAL(KEY_UP, e.key);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_UP, false, HOLD + 200).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_DOWN, false, HOLD + 300).type);
}

static void test_fn_press_does_not_rearm_chord(void)
{
    init();
    kp_on_change(&s, KEY_UP, true, 0);
    kp_on_change(&s, KEY_DOWN, true, 100);
    TEST_ASSERT_EQUAL(KP_EVT_CHORD_REVERSE, kp_on_tick(&s, 100 + LONG).type);
    kp_on_change(&s, KEY_FN, true, 100 + LONG + 100);
    kp_event_t e = kp_on_tick(&s, 100 + LONG + 100 + LONG + 100);
    TEST_ASSERT_TRUE(e.type != KP_EVT_CHORD_REVERSE);   /* no duplicate toggle */
}

static void test_all_three_suppress_chord_and_fn_long(void)
{
    /* Replaces test_fn_long_fires_during_chord. Holding all three keys is now
     * the factory-reset gesture, so neither the calibration long-press nor the
     * reverse chord may fire on the way — CHORD_REVERSE would wipe the
     * calibration before the 5 s reset ever lands. */
    init();
    kp_on_change(&s, KEY_FN, true, 0);
    kp_on_change(&s, KEY_UP, true, 100);
    kp_on_change(&s, KEY_DOWN, true, 200);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, LONG + 10).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, 200 + LONG + 10).type);
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

static void test_all_three_released_early_emits_nothing(void)
{
    init();
    kp_on_change(&s, KEY_FN, true, 0);
    kp_on_change(&s, KEY_UP, true, 100);
    kp_on_change(&s, KEY_DOWN, true, 200);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_DOWN, false, 3000).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_UP, false, 3050).type);
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_FN, false, 3100).type);
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

/* ---- the "works only on the second click" signature ------------------- *
 *
 * Characterisation, not a desired behaviour: the release that CLEARS the
 * in_reset latch is itself suppressed by the same early return that cleared
 * it, so the first complete press after a three-key gesture is swallowed and
 * only the second does anything. Reported from the bench 2026-08-12 as "the
 * function key seems to be effective only second click".
 */
static void test_the_release_that_clears_in_reset_is_itself_suppressed(void)
{
    init();
    kp_on_change(&s, KEY_UP,   true, 1000);
    kp_on_change(&s, KEY_DOWN, true, 1010);
    kp_on_change(&s, KEY_FN,   true, 1020);
    TEST_ASSERT_TRUE(s.in_reset);

    kp_on_change(&s, KEY_UP,   false, 1100);
    kp_on_change(&s, KEY_DOWN, false, 1110);
    /* The release completing the set clears the latch and is swallowed by the
     * same early return. Whichever key the operator lets go of last, that
     * press produced nothing — one click spent on recovery. */
    TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_FN, false, 1120).type);
    TEST_ASSERT_FALSE(s.in_reset);

    /* and only the next press does anything */
    kp_on_change(&s, KEY_FN, true, 2000);
    TEST_ASSERT_EQUAL(KP_EVT_TAP, kp_on_change(&s, KEY_FN, false, 2100).type);
}

/* The failure that makes it permanent rather than a one-off: if ONE release
 * never arrives — swallowed by the filter, or lost — down[] never returns to
 * all-false, in_reset never clears, and every key stays dead until reboot.
 * This is the stuck-keypad candidate, and the reason keypad.c now traces the
 * latch flags: no other record can see this state. */
static void test_a_missed_release_latches_in_reset_forever(void)
{
    init();
    kp_on_change(&s, KEY_UP,   true, 1000);
    kp_on_change(&s, KEY_DOWN, true, 1010);
    kp_on_change(&s, KEY_FN,   true, 1020);
    kp_on_change(&s, KEY_UP,   false, 1100);
    kp_on_change(&s, KEY_FN,   false, 1120);
    /* KEY_DOWN's release never arrives */

    TEST_ASSERT_TRUE(s.in_reset);

    /* every subsequent gesture on every key is silent, indefinitely */
    for (uint32_t t = 2000; t < 60000; t += 1000) {
        kp_on_change(&s, KEY_FN, true, t);
        TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_change(&s, KEY_FN, false, t + 100).type);
        TEST_ASSERT_EQUAL(KP_EVT_NONE, kp_on_tick(&s, t + 200).type);
    }
    TEST_ASSERT_TRUE(s.in_reset);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_short_press_is_tap);
    RUN_TEST(test_hold_emits_start_then_end_not_tap);
    RUN_TEST(test_fn_long_press_fires_once_no_hold_events);
    RUN_TEST(test_fn_short_press_is_tap);
    RUN_TEST(test_fn_medium_press_is_still_tap);
    RUN_TEST(test_chord_fires_once_and_suppresses_up_down_events);
    RUN_TEST(test_two_keys_without_long_hold_are_independent);
    RUN_TEST(test_chord_during_jog_ends_hold);
    RUN_TEST(test_fn_press_does_not_rearm_chord);
    RUN_TEST(test_all_three_suppress_chord_and_fn_long);
    RUN_TEST(test_all_three_held_emits_factory_reset_once);
    RUN_TEST(test_all_three_released_early_emits_nothing);
    RUN_TEST(test_fn_long_suppressed_while_up_held);
    RUN_TEST(test_reset_gesture_rearms_after_full_release);
    RUN_TEST(test_reset_timer_restarts_when_trio_recompletes);
    RUN_TEST(test_the_release_that_clears_in_reset_is_itself_suppressed);
    RUN_TEST(test_a_missed_release_latches_in_reset_forever);
    return UNITY_END();
}
