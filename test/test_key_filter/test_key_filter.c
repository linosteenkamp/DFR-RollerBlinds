#include <unity.h>
#include "../../src/key_filter.c"

void setUp(void) {}
void tearDown(void) {}

#define MAX 12

static key_filter_t f;

/* Feed n samples at one level; return how many times the output flipped. */
static int feed(int level, int n)
{
    int flips = 0;
    for (int i = 0; i < n; i++) {
        if (key_filter_sample(&f, level)) flips++;
    }
    return flips;
}

/* The reported bug: one aberrant sample used to be a complete press. */
static void test_single_sample_glitch_is_rejected(void)
{
    key_filter_init(&f, MAX, 1);
    TEST_ASSERT_FALSE(key_filter_sample(&f, 0));   /* the glitch */
    TEST_ASSERT_FALSE(key_filter_sample(&f, 1));   /* line recovers */
    TEST_ASSERT_EQUAL(1, key_filter_level(&f));
}

static void test_sustained_low_flips_once_on_the_max_th_sample(void)
{
    key_filter_init(&f, MAX, 1);
    TEST_ASSERT_EQUAL(0, feed(0, MAX - 1));
    TEST_ASSERT_EQUAL(1, key_filter_level(&f));
    TEST_ASSERT_TRUE(key_filter_sample(&f, 0));
    TEST_ASSERT_EQUAL(0, key_filter_level(&f));
}

static void test_no_repeat_events_while_level_holds(void)
{
    key_filter_init(&f, MAX, 1);
    TEST_ASSERT_EQUAL(1, feed(0, MAX));
    TEST_ASSERT_EQUAL(0, feed(0, 50));
    TEST_ASSERT_EQUAL(0, key_filter_level(&f));
}

static void test_rattling_line_never_flips(void)
{
    key_filter_init(&f, MAX, 1);
    int flips = 0;
    for (int i = 0; i < 500; i++) {
        if (key_filter_sample(&f, i & 1)) flips++;
    }
    TEST_ASSERT_EQUAL(0, flips);
    TEST_ASSERT_EQUAL(1, key_filter_level(&f));
}

static void test_partial_excursion_retreats(void)
{
    key_filter_init(&f, MAX, 1);
    TEST_ASSERT_EQUAL(0, feed(0, MAX - 1));   /* one short of the rail */
    TEST_ASSERT_EQUAL(0, feed(1, MAX - 1));   /* and back again */
    TEST_ASSERT_EQUAL(1, key_filter_level(&f));
}

static void test_release_is_symmetric(void)
{
    key_filter_init(&f, MAX, 1);
    TEST_ASSERT_EQUAL(1, feed(0, MAX));
    TEST_ASSERT_EQUAL(0, key_filter_level(&f));
    TEST_ASSERT_EQUAL(0, feed(1, MAX - 1));
    TEST_ASSERT_TRUE(key_filter_sample(&f, 1));
    TEST_ASSERT_EQUAL(1, key_filter_level(&f));
}

/* Without clamping, a long press would drive the counter far past the rail
 * and the release would then take hundreds of samples to register. */
static void test_counter_clamps_at_the_rails(void)
{
    key_filter_init(&f, MAX, 1);
    feed(0, 100);
    TEST_ASSERT_EQUAL(0, key_filter_level(&f));
    TEST_ASSERT_EQUAL(0, feed(1, MAX - 1));
    TEST_ASSERT_TRUE(key_filter_sample(&f, 1));
}

static void test_init_seeds_at_the_rail_with_no_spurious_event(void)
{
    key_filter_init(&f, MAX, 1);
    TEST_ASSERT_EQUAL(1, key_filter_level(&f));
    TEST_ASSERT_EQUAL(0, feed(1, 20));

    key_filter_init(&f, MAX, 0);
    TEST_ASSERT_EQUAL(0, key_filter_level(&f));
    TEST_ASSERT_EQUAL(0, feed(0, 20));
}

/* Equal latency at both edges means keypad_logic measures the true press
 * duration, so HOLD_MS and LONG_MS keep their current feel — BUT note the
 * stimulus: a perfect square wave. Symmetry is a property of the input here,
 * not a guarantee of the filter. test_ragged_edge_costs_more_than_a_clean_one
 * below is the counterexample, and test_press_classification/ shows what it
 * does to a gesture. */
static void test_latency_is_symmetric_so_duration_is_preserved(void)
{
    key_filter_init(&f, MAX, 1);
    int press_at = -1, release_at = -1;
    for (int i = 0; i < 200; i++) {
        int raw = (i < 100) ? 0 : 1;          /* contact for 100 samples */
        if (key_filter_sample(&f, raw)) {
            if (press_at < 0) press_at = i; else release_at = i;
        }
    }
    TEST_ASSERT_EQUAL(11, press_at);
    TEST_ASSERT_EQUAL(111, release_at);
    TEST_ASSERT_EQUAL(100, release_at - press_at);
}

/* ---- telemetry: the two ways a real press goes missing ---------------- */

/* Rattle toward the far rail: `hi` high samples then `lo` low, repeated, so
 * the counter makes net progress of (hi - lo) per group without ever settling.
 * Stops at the flip and returns the samples it took, -1 if it never got there. */
static int feed_ragged_until_flip(int hi, int lo, int budget)
{
    for (int n = 0; n < budget; n++) {
        int level = (n % (hi + lo)) < hi ? 1 : 0;
        if (key_filter_sample(&f, level)) return n + 1;
    }
    return -1;
}

static void test_clean_edge_traverses_in_exactly_max_samples(void)
{
    key_filter_init(&f, MAX, 1);
    TEST_ASSERT_EQUAL(1, feed(0, MAX));
    TEST_ASSERT_EQUAL(MAX, key_filter_traverse(&f));
}

/* The asymmetry the symmetric-latency test cannot see: the same rail-to-rail
 * distance costs far more samples when the line dithers on the way. A release
 * edge slower than its make edge inflates the duration keypad_logic measures. */
static void test_ragged_edge_costs_more_than_a_clean_one(void)
{
    key_filter_init(&f, MAX, 1);
    TEST_ASSERT_EQUAL(1, feed(0, MAX));          /* clean make: MAX samples */
    TEST_ASSERT_EQUAL(MAX, key_filter_traverse(&f));

    /* release dithers: 2 high, 1 low → net +1 per 3 samples, so the same 12
     * units of distance cost 32 samples instead of 12 */
    TEST_ASSERT_EQUAL(32, feed_ragged_until_flip(2, 1, 200));
    TEST_ASSERT_EQUAL(1, key_filter_level(&f));
    TEST_ASSERT_EQUAL(32, key_filter_traverse(&f));
}

static void test_abandoned_excursion_reports_how_close_it_came(void)
{
    key_filter_init(&f, MAX, 1);
    TEST_ASSERT_EQUAL(0, feed(0, MAX - 1));   /* one sample short of a press */
    TEST_ASSERT_EQUAL(0, key_filter_take_swallowed(&f));  /* still out there */

    TEST_ASSERT_EQUAL(0, feed(1, MAX - 1));   /* gives up and goes home */
    TEST_ASSERT_EQUAL(MAX - 1, key_filter_take_swallowed(&f));
}

/* Depth is the whole point: it separates a press that fell just short from
 * line noise the integrator is supposed to reject. */
static void test_shallow_noise_reports_a_shallow_depth(void)
{
    key_filter_init(&f, MAX, 1);
    feed(0, 2);
    feed(1, 2);
    TEST_ASSERT_EQUAL(2, key_filter_take_swallowed(&f));
}

static void test_a_completed_press_is_not_swallowed(void)
{
    key_filter_init(&f, MAX, 1);
    TEST_ASSERT_EQUAL(1, feed(0, MAX));
    TEST_ASSERT_EQUAL(0, key_filter_take_swallowed(&f));
}

static void test_take_swallowed_clears_so_each_is_seen_once(void)
{
    key_filter_init(&f, MAX, 1);
    feed(0, MAX - 1);
    feed(1, MAX - 1);
    TEST_ASSERT_EQUAL(MAX - 1, key_filter_take_swallowed(&f));
    TEST_ASSERT_EQUAL(0, key_filter_take_swallowed(&f));
}

/* A rattling line that never crosses still returns to the rail repeatedly, so
 * the caller polling every sample sees each attempt rather than one summary. */
static void test_repeated_attempts_are_each_reported(void)
{
    key_filter_init(&f, MAX, 1);
    for (int attempt = 0; attempt < 3; attempt++) {
        feed(0, 4);
        feed(1, 4);
        TEST_ASSERT_EQUAL(4, key_filter_take_swallowed(&f));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_single_sample_glitch_is_rejected);
    RUN_TEST(test_sustained_low_flips_once_on_the_max_th_sample);
    RUN_TEST(test_no_repeat_events_while_level_holds);
    RUN_TEST(test_rattling_line_never_flips);
    RUN_TEST(test_partial_excursion_retreats);
    RUN_TEST(test_release_is_symmetric);
    RUN_TEST(test_counter_clamps_at_the_rails);
    RUN_TEST(test_init_seeds_at_the_rail_with_no_spurious_event);
    RUN_TEST(test_latency_is_symmetric_so_duration_is_preserved);
    RUN_TEST(test_clean_edge_traverses_in_exactly_max_samples);
    RUN_TEST(test_ragged_edge_costs_more_than_a_clean_one);
    RUN_TEST(test_abandoned_excursion_reports_how_close_it_came);
    RUN_TEST(test_shallow_noise_reports_a_shallow_depth);
    RUN_TEST(test_a_completed_press_is_not_swallowed);
    RUN_TEST(test_take_swallowed_clears_so_each_is_seen_once);
    RUN_TEST(test_repeated_attempts_are_each_reported);
    return UNITY_END();
}
