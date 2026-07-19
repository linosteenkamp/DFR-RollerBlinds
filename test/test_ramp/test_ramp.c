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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_long_move_reaches_and_holds_cruise);
    RUN_TEST(test_profile_is_symmetric);
    RUN_TEST(test_accel_intervals_monotonically_decrease);
    RUN_TEST(test_short_move_becomes_triangle_never_reaches_cruise);
    RUN_TEST(test_single_step_move);
    return UNITY_END();
}
