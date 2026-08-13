/**
 * Characterisation tests for key_filter -> keypad_logic, at the real poll rate
 * and the real FILTER_SAMPLES/HOLD_MS the firmware ships.
 *
 * These document CURRENT behaviour rather than asserting desired behaviour.
 * They exist because the debounce integrator and the gesture classifier are
 * each correct in isolation and host-tested that way, while the defect being
 * chased lives in what one hands the other: the classifier decides TAP vs HOLD
 * from the interval between two flips, so any difference in latency between
 * the make edge and the break edge lands directly on that decision.
 *
 * If a fix later changes these outcomes, that is the point — update them
 * deliberately and say so in the commit.
 */
#include <unity.h>
#include "../../src/key_filter.c"
#include "../../src/keypad_logic.c"

/* Mirrors src/keypad.c. Kept as literals rather than shared constants so a
 * change there shows up here as a failure instead of silently retuning the
 * scenario the test is meant to pin down. */
#define POLL_MS        5
#define FILTER_SAMPLES 12
#define HOLD_MS        400
#define LONG_MS        3000
#define RESET_MS       5000

static key_filter_t f;
static kp_state_t   kp;
static uint32_t     now_ms;

static kp_event_type_t seen[32];
static int             n_seen;

void setUp(void)
{
    key_filter_init(&f, FILTER_SAMPLES, 1);   /* idle high, active-low keys */
    kp_init(&kp, HOLD_MS, LONG_MS, RESET_MS);
    now_ms = 0;
    n_seen = 0;
}
void tearDown(void) {}

static void record(kp_event_t e)
{
    if (e.type != KP_EVT_NONE && n_seen < (int)(sizeof seen / sizeof seen[0])) {
        seen[n_seen++] = e.type;
    }
}

/* One poll tick, exactly as poll_cb does it. */
static void poll(int raw_level)
{
    if (key_filter_sample(&f, raw_level)) {
        record(kp_on_change(&kp, KEY_UP, key_filter_level(&f) == 0, now_ms));
    }
    record(kp_on_tick(&kp, now_ms));
    now_ms += POLL_MS;
}

static void hold_level(int raw_level, uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += POLL_MS) poll(raw_level);
}

/* A break that dithers across the input threshold as the finger lifts:
 * 2 samples high, 1 low, repeated. Net progress, but 2.7x the samples. */
static void release_ragged(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += POLL_MS) {
        poll(((t / POLL_MS) % 3) < 2 ? 1 : 0);
    }
}

/* ---------------------------------------------------------------------- */

/* Baseline: clean edges both ways, and a 350 ms press is a TAP -> full travel.
 * This is what the operator expects from a click. */
static void test_clean_350ms_press_is_a_tap(void)
{
    hold_level(0, 350);
    hold_level(1, 200);

    TEST_ASSERT_EQUAL(1, n_seen);
    TEST_ASSERT_EQUAL(KP_EVT_TAP, seen[0]);
}

/* THE MECHANISM. The identical 350 ms contact, released raggedly, is no longer
 * a tap: the break edge takes ~160 ms longer than the make edge did, the
 * measured interval crosses HOLD_MS, and the gesture becomes a jog of the
 * remainder — roughly 70 ms of motion, a fraction of a percent of a span.
 *
 * To the operator that is a click that did nothing. Nothing downstream can
 * tell it apart from a dead key, and the trace records it as a legitimate
 * hold, because that is exactly what the classifier was told. */
static void test_same_press_released_raggedly_becomes_an_invisible_jog(void)
{
    hold_level(0, 350);
    release_ragged(400);

    TEST_ASSERT_EQUAL(2, n_seen);
    TEST_ASSERT_EQUAL(KP_EVT_HOLD_START, seen[0]);
    TEST_ASSERT_EQUAL(KP_EVT_HOLD_END, seen[1]);
}

/* The other way a press goes missing, and the reason for the swallowed-press
 * telemetry: a contact shorter than the filter window never reaches the rail,
 * so no event is ever emitted. The change detector this replaced would have
 * called a single low sample a complete press. */
static void test_press_shorter_than_the_filter_window_emits_nothing(void)
{
    hold_level(0, 40);          /* 8 samples: short of the 12 needed */
    hold_level(1, 200);

    TEST_ASSERT_EQUAL(0, n_seen);
    TEST_ASSERT_EQUAL(8, key_filter_take_swallowed(&f));
}

/* ...and that it is genuinely a near-miss rather than noise is visible in the
 * depth, which is what keypad.c thresholds on before spending a trace slot. */
static void test_a_near_miss_is_distinguishable_from_line_noise(void)
{
    hold_level(0, 55);          /* 11 samples: one short */
    hold_level(1, 100);
    TEST_ASSERT_EQUAL(11, key_filter_take_swallowed(&f));

    setUp();
    poll(0);                    /* a single coupled glitch */
    hold_level(1, 100);
    TEST_ASSERT_EQUAL(1, key_filter_take_swallowed(&f));
}

/* A long press is unaffected: it was always going to be a hold, and a ragged
 * break only extends a jog that was already visible. The defect is specific to
 * presses landing near HOLD_MS, which is where ordinary clicks land. */
static void test_a_long_press_is_a_hold_either_way(void)
{
    hold_level(0, 1000);
    release_ragged(400);

    TEST_ASSERT_EQUAL(2, n_seen);
    TEST_ASSERT_EQUAL(KP_EVT_HOLD_START, seen[0]);
    TEST_ASSERT_EQUAL(KP_EVT_HOLD_END, seen[1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_clean_350ms_press_is_a_tap);
    RUN_TEST(test_same_press_released_raggedly_becomes_an_invisible_jog);
    RUN_TEST(test_press_shorter_than_the_filter_window_emits_nothing);
    RUN_TEST(test_a_near_miss_is_distinguishable_from_line_noise);
    RUN_TEST(test_a_long_press_is_a_hold_either_way);
    return UNITY_END();
}
