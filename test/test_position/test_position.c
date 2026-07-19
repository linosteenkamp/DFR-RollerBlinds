#include <unity.h>
#include "../../src/position.c"

void setUp(void) {}
void tearDown(void) {}

#define MIN_SPAN 6000

static position_t fresh(void)   /* factory-new device */
{
    position_t p;
    position_init(&p, false, 0, false, 0);
    return p;
}

static position_t calibrated(void)   /* span 24000, sitting at Open */
{
    position_t p;
    position_init(&p, true, 24000, true, 0);
    return p;
}

static void test_fresh_device_is_uncalibrated_unknown_lift(void)
{
    position_t p = fresh();
    TEST_ASSERT_FALSE(position_calibrated(&p));
    TEST_ASSERT_EQUAL_UINT8(POSITION_LIFT_UNKNOWN, position_lift_pct(&p));
}

static void test_calibrated_lift_math_and_rounding(void)
{
    position_t p = calibrated();
    TEST_ASSERT_TRUE(position_calibrated(&p));
    TEST_ASSERT_EQUAL_UINT8(0, position_lift_pct(&p));
    position_set_current(&p, 24000);
    TEST_ASSERT_EQUAL_UINT8(100, position_lift_pct(&p));
    position_set_current(&p, 12000);
    TEST_ASSERT_EQUAL_UINT8(50, position_lift_pct(&p));
    position_set_current(&p, 12120);   /* 50.5% -> rounds to nearest */
    TEST_ASSERT_EQUAL_UINT8(51, position_lift_pct(&p));
}

static void test_target_for_pct_and_clamp(void)
{
    position_t p = calibrated();
    TEST_ASSERT_EQUAL_INT32(0, position_target_for_pct(&p, 0));
    TEST_ASSERT_EQUAL_INT32(24000, position_target_for_pct(&p, 100));
    TEST_ASSERT_EQUAL_INT32(12000, position_target_for_pct(&p, 50));
    TEST_ASSERT_EQUAL_INT32(24000, position_target_for_pct(&p, 150)); /* clamped */
    TEST_ASSERT_EQUAL_INT32(0, position_clamp(&p, -500));
    TEST_ASSERT_EQUAL_INT32(24000, position_clamp(&p, 99999));
}

static void test_full_calibration_happy_path(void)
{
    position_t p = fresh();
    position_cal_enter(&p);
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK1, p.cal);
    TEST_ASSERT_TRUE(position_cal_mark(&p, 1000, MIN_SPAN));   /* Open mark at raw 1000 */
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK2, p.cal);
    TEST_ASSERT_TRUE(position_cal_mark(&p, 25000, MIN_SPAN));  /* Closed mark */
    TEST_ASSERT_EQUAL(POS_CAL_NONE, p.cal);
    TEST_ASSERT_TRUE(position_calibrated(&p));
    TEST_ASSERT_EQUAL_INT32(24000, p.closed_steps);
    TEST_ASSERT_EQUAL_UINT8(100, position_lift_pct(&p));       /* at Closed after cal */
}

static void test_mark2_above_or_too_close_rejected_stays_in_mode(void)
{
    position_t p = fresh();
    position_cal_enter(&p);
    TEST_ASSERT_TRUE(position_cal_mark(&p, 10000, MIN_SPAN));
    TEST_ASSERT_FALSE(position_cal_mark(&p, 9000, MIN_SPAN));   /* above mark1 */
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK2, p.cal);               /* still waiting */
    TEST_ASSERT_FALSE(position_cal_mark(&p, 10000 + MIN_SPAN - 1, MIN_SPAN)); /* too short */
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK2, p.cal);
    TEST_ASSERT_TRUE(position_cal_mark(&p, 10000 + MIN_SPAN, MIN_SPAN));      /* boundary OK */
    TEST_ASSERT_TRUE(position_calibrated(&p));
}

static void test_recal_abort_keeps_previous_calibration(void)
{
    position_t p = calibrated();
    position_cal_enter(&p);                       /* calibrated -> full recal */
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK1, p.cal);
    TEST_ASSERT_TRUE(position_cal_mark(&p, 500, MIN_SPAN));
    position_cal_abort(&p);
    TEST_ASSERT_EQUAL(POS_CAL_NONE, p.cal);
    TEST_ASSERT_TRUE(position_calibrated(&p));    /* old span survives */
    TEST_ASSERT_EQUAL_INT32(24000, p.closed_steps);
}

static void test_rehome_entry_when_position_unknown(void)
{
    position_t p = calibrated();
    position_mark_unknown(&p);                    /* power died mid-move */
    TEST_ASSERT_FALSE(position_calibrated(&p));
    TEST_ASSERT_EQUAL_UINT8(POSITION_LIFT_UNKNOWN, position_lift_pct(&p));
    position_cal_enter(&p);
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_REHOME, p.cal); /* one mark, span kept */
    TEST_ASSERT_TRUE(position_cal_mark(&p, -3210, MIN_SPAN)); /* raw value irrelevant */
    TEST_ASSERT_TRUE(position_calibrated(&p));
    TEST_ASSERT_EQUAL_INT32(24000, p.closed_steps);
    TEST_ASSERT_EQUAL_UINT8(0, position_lift_pct(&p));        /* re-homed at Open */
}

static void test_wipe_forces_full_recal_not_rehome(void)
{
    position_t p = calibrated();
    position_wipe(&p);                            /* Motor Reversed toggled */
    TEST_ASSERT_FALSE(position_calibrated(&p));
    position_cal_enter(&p);
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK1, p.cal); /* span wiped -> two marks */
}

static void test_zero_min_span_cannot_commit_empty_span(void)
{
    position_t p = fresh();
    position_cal_enter(&p);
    TEST_ASSERT_TRUE(position_cal_mark(&p, 5000, 0));
    TEST_ASSERT_FALSE(position_cal_mark(&p, 5000, 0));  /* zero span rejected */
    TEST_ASSERT_EQUAL(POS_CAL_WAIT_MARK2, p.cal);
    TEST_ASSERT_FALSE(position_calibrated(&p));
}

static void test_mark_outside_calibration_is_inert(void)
{
    position_t p = calibrated();
    TEST_ASSERT_FALSE(position_cal_mark(&p, 123, MIN_SPAN));
    TEST_ASSERT_TRUE(position_calibrated(&p));
    TEST_ASSERT_EQUAL_INT32(0, p.cur_steps);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fresh_device_is_uncalibrated_unknown_lift);
    RUN_TEST(test_calibrated_lift_math_and_rounding);
    RUN_TEST(test_target_for_pct_and_clamp);
    RUN_TEST(test_full_calibration_happy_path);
    RUN_TEST(test_mark2_above_or_too_close_rejected_stays_in_mode);
    RUN_TEST(test_recal_abort_keeps_previous_calibration);
    RUN_TEST(test_rehome_entry_when_position_unknown);
    RUN_TEST(test_wipe_forces_full_recal_not_rehome);
    RUN_TEST(test_mark_outside_calibration_is_inert);
    RUN_TEST(test_zero_min_span_cannot_commit_empty_span);
    return UNITY_END();
}
