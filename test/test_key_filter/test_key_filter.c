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

/* Equal latency at both edges means keypad_logic still measures the true
 * press duration, so HOLD_MS and LONG_MS keep their current feel. */
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
    return UNITY_END();
}
