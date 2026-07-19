#include <unity.h>
#include "../../src/keypad_logic.c"

void setUp(void) {}
void tearDown(void) {}

#define HOLD 400
#define LONG 3000

static kp_state_t s;

static void init(void) { kp_init(&s, HOLD, LONG); }

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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_short_press_is_tap);
    RUN_TEST(test_hold_emits_start_then_end_not_tap);
    RUN_TEST(test_fn_long_press_fires_once_no_hold_events);
    RUN_TEST(test_fn_short_press_is_tap);
    RUN_TEST(test_chord_fires_once_and_suppresses_up_down_events);
    RUN_TEST(test_two_keys_without_long_hold_are_independent);
    return UNITY_END();
}
