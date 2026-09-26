#include <unity.h>
#include "../../src/ramp.c"

void setUp(void) {}
void tearDown(void) {}

/* Bench-plausible numbers: start 2 kHz (500 µs), cruise ~3.3 kHz (300 µs). */
#define START_US  500
#define CRUISE_US 300

static void test_long_move_reaches_and_holds_cruise(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 10000, CRUISE_US, START_US, 800);
    TEST_ASSERT_EQUAL_UINT32(START_US, ramp_interval_us(&r, 0));
    TEST_ASSERT_EQUAL_UINT32(CRUISE_US, ramp_interval_us(&r, 800));   /* end of accel */
    TEST_ASSERT_EQUAL_UINT32(CRUISE_US, ramp_interval_us(&r, 5000));  /* mid cruise */
    TEST_ASSERT_EQUAL_UINT32(START_US, ramp_interval_us(&r, 9999));   /* last step */
}

static void test_profile_is_symmetric(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 10000, CRUISE_US, START_US, 800);
    for (int32_t i = 0; i < 800; i += 37) {
        TEST_ASSERT_EQUAL_UINT32(ramp_interval_us(&r, i),
                                 ramp_interval_us(&r, r.total - 1 - i));
    }
}

static void test_accel_intervals_monotonically_decrease(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 10000, CRUISE_US, START_US, 800);
    uint32_t prev = ramp_interval_us(&r, 0);
    for (int32_t i = 1; i <= 800; i++) {
        uint32_t cur = ramp_interval_us(&r, i);
        TEST_ASSERT_TRUE(cur <= prev);
        prev = cur;
    }
}

static void test_short_move_becomes_triangle_never_reaches_cruise(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 100, CRUISE_US, START_US, 800);   /* accel clamped to 50 */
    TEST_ASSERT_EQUAL_INT32(50, r.accel_steps);
    for (int32_t i = 0; i < 100; i++) {
        TEST_ASSERT_TRUE(ramp_interval_us(&r, i) > CRUISE_US);
    }
    /* peak (fastest) at the apex, edges slowest */
    TEST_ASSERT_TRUE(ramp_interval_us(&r, 50) < ramp_interval_us(&r, 0));
}

static void test_single_step_move(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 1, CRUISE_US, START_US, 800);
    TEST_ASSERT_EQUAL_UINT32(START_US, ramp_interval_us(&r, 0));
}

static void test_cruise_slower_than_start_is_clamped(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 1000, 600, 500, 800);   /* cruise SLOWER than start */
    TEST_ASSERT_EQUAL_UINT32(600, r.start_us); /* clamped */
    for (int32_t i = 0; i < 1000; i += 13) {
        uint32_t iv = ramp_interval_us(&r, i);
        TEST_ASSERT_TRUE(iv >= 600);           /* never faster than cruise, never 0 */
    }
}

/* Travel-time conversion. Span 300000 steps chosen so the arithmetic is
 * exact: 30 s -> 100 us, 60 s -> 200 us. */
#define SPAN_STEPS 300000

static void test_travel_time_converts_to_cruise_interval(void)
{
    TEST_ASSERT_EQUAL_UINT32(100, ramp_us_from_travel_time(30, SPAN_STEPS));
    TEST_ASSERT_EQUAL_UINT32(200, ramp_us_from_travel_time(60, SPAN_STEPS));
}

static void test_travel_time_clamps_to_min_cruise(void)
{
    /* 10 s over this span wants 33 us — below the floor. */
    TEST_ASSERT_EQUAL_UINT32(RAMP_MIN_CRUISE_US,
                             ramp_us_from_travel_time(10, SPAN_STEPS));
}

static void test_travel_time_clamps_to_max_cruise(void)
{
    /* 400 s wants 1333 us — above the ceiling. */
    TEST_ASSERT_EQUAL_UINT32(RAMP_MAX_CRUISE_US,
                             ramp_us_from_travel_time(400, SPAN_STEPS));
}

static void test_travel_time_just_above_floor_passes_through(void)
{
    /* 18 s lands exactly on the floor, 19 s just above it. The second
     * assertion is the one that carries weight: a value near the bound must
     * come through the arithmetic unchanged rather than being flattened to
     * the clamp, which asserting only the exact-bound case cannot show. */
    TEST_ASSERT_EQUAL_UINT32(RAMP_MIN_CRUISE_US,
                             ramp_us_from_travel_time(18, SPAN_STEPS));
    TEST_ASSERT_EQUAL_UINT32(63, ramp_us_from_travel_time(19, SPAN_STEPS));
}

static void test_travel_time_without_span_returns_default(void)
{
    TEST_ASSERT_EQUAL_UINT32(RAMP_DEFAULT_CRUISE_US,
                             ramp_us_from_travel_time(30, 0));
    TEST_ASSERT_EQUAL_UINT32(RAMP_DEFAULT_CRUISE_US,
                             ramp_us_from_travel_time(30, -1));
}

static void test_zero_travel_time_returns_default(void)
{
    /* 0 = "never set" — the device keeps its compile-time speed. */
    TEST_ASSERT_EQUAL_UINT32(RAMP_DEFAULT_CRUISE_US,
                             ramp_us_from_travel_time(0, SPAN_STEPS));
}

static void test_cruise_interval_converts_back_to_travel_time(void)
{
    TEST_ASSERT_EQUAL_UINT16(30, ramp_travel_time_from_us(100, SPAN_STEPS));
    TEST_ASSERT_EQUAL_UINT16(45, ramp_travel_time_from_us(150, SPAN_STEPS));
}

static void test_travel_time_round_trips(void)
{
    uint32_t us = ramp_us_from_travel_time(30, SPAN_STEPS);
    TEST_ASSERT_EQUAL_UINT16(30, ramp_travel_time_from_us(us, SPAN_STEPS));
}

static void test_travel_time_from_us_without_span_is_zero(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, ramp_travel_time_from_us(150, 0));
}

static void test_travel_time_round_trip_does_not_lose_a_second(void)
{
    /* A span whose division is inexact both ways. 55 s wants 150.0003 us,
     * truncated to 150; 150 us over this span is then 54.9999 s, which a
     * truncating inverse would report as 54. Rounding is what keeps the
     * round trip honest — and it matters because the operator docs say a
     * value settling different from the request means the clamp was hit. */
    const int32_t span = 366666;
    uint32_t us = ramp_us_from_travel_time(55, span);
    TEST_ASSERT_EQUAL_UINT16(55, ramp_travel_time_from_us(us, span));
}

/* ---------- stop path (used by the motion ISR) ---------- */

static void test_stop_index_decelerates_as_far_as_it_accelerated(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 10000, CRUISE_US, START_US, 800);
    TEST_ASSERT_EQUAL_INT32(1, ramp_stop_index(&r, 0));       /* at rest: one step */
    TEST_ASSERT_EQUAL_INT32(800, ramp_stop_index(&r, 400));   /* mid-accel */
    TEST_ASSERT_EQUAL_INT32(5800, ramp_stop_index(&r, 5000)); /* cruise: full ramp */
    TEST_ASSERT_EQUAL_INT32(10000, ramp_stop_index(&r, 9990));/* capped by the plan */
}

static void test_stop_index_triangle_profile(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 1000, CRUISE_US, START_US, 800);       /* accel clamps to 500 */
    TEST_ASSERT_EQUAL_INT32(500, ramp_stop_index(&r, 250));
}

static void test_stopping_interval_is_continuous_at_the_stop(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 10000, CRUISE_US, START_US, 800);
    /* stop requested mid-accel at 400: no jump in speed at that step */
    TEST_ASSERT_EQUAL_UINT32(ramp_interval_us(&r, 400),
                             ramp_interval_us_stopping(&r, 400, 800));
    /* stop requested at cruise */
    TEST_ASSERT_EQUAL_UINT32(CRUISE_US, ramp_interval_us_stopping(&r, 5000, 5800));
}

static void test_stopping_interval_mirrors_the_accel_ramp(void)
{
    ramp_plan_t r;
    ramp_plan_init(&r, 10000, CRUISE_US, START_US, 800);
    for (int32_t k = 0; k < 800; k += 13) {
        TEST_ASSERT_EQUAL_UINT32(ramp_interval_us(&r, 800 - k),
                                 ramp_interval_us_stopping(&r, 5000 + k, 5800));
    }
    TEST_ASSERT_EQUAL_UINT32(START_US, ramp_interval_us_stopping(&r, 5800, 5800));
    TEST_ASSERT_EQUAL_UINT32(START_US, ramp_interval_us_stopping(&r, 5900, 5800));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_long_move_reaches_and_holds_cruise);
    RUN_TEST(test_profile_is_symmetric);
    RUN_TEST(test_accel_intervals_monotonically_decrease);
    RUN_TEST(test_short_move_becomes_triangle_never_reaches_cruise);
    RUN_TEST(test_single_step_move);
    RUN_TEST(test_cruise_slower_than_start_is_clamped);
    RUN_TEST(test_travel_time_converts_to_cruise_interval);
    RUN_TEST(test_travel_time_clamps_to_min_cruise);
    RUN_TEST(test_travel_time_clamps_to_max_cruise);
    RUN_TEST(test_travel_time_just_above_floor_passes_through);
    RUN_TEST(test_travel_time_without_span_returns_default);
    RUN_TEST(test_zero_travel_time_returns_default);
    RUN_TEST(test_cruise_interval_converts_back_to_travel_time);
    RUN_TEST(test_travel_time_round_trips);
    RUN_TEST(test_travel_time_from_us_without_span_is_zero);
    RUN_TEST(test_travel_time_round_trip_does_not_lose_a_second);
    RUN_TEST(test_stop_index_decelerates_as_far_as_it_accelerated);
    RUN_TEST(test_stop_index_triangle_profile);
    RUN_TEST(test_stopping_interval_is_continuous_at_the_stop);
    RUN_TEST(test_stopping_interval_mirrors_the_accel_ramp);
    return UNITY_END();
}
