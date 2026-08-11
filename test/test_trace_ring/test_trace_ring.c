#include <unity.h>
#include <string.h>
#include "../../src/trace_ring.c"

void setUp(void) {}
void tearDown(void) {}

#define MAGIC 0x54524331u

static trace_ring_t r;

/* The cold-boot case. RTC_NOINIT holds garbage at power-on, and if this
 * passes validation the very first dump prints noise indistinguishable
 * from real data — worse than having no tool at all. */
static void test_uninitialised_ring_is_not_valid(void)
{
    memset(&r, 0xA5, sizeof r);
    TEST_ASSERT_FALSE(trace_ring_valid(&r, MAGIC));
}

/* Garbage that happens to collide with the magic must still be rejected
 * on its out-of-range bookkeeping. */
static void test_right_magic_but_bogus_indices_is_not_valid(void)
{
    memset(&r, 0xA5, sizeof r);
    r.magic = MAGIC;
    TEST_ASSERT_FALSE(trace_ring_valid(&r, MAGIC));
}

static void test_reset_makes_valid_and_empty(void)
{
    memset(&r, 0xA5, sizeof r);
    trace_ring_reset(&r, MAGIC);
    TEST_ASSERT_TRUE(trace_ring_valid(&r, MAGIC));
    TEST_ASSERT_EQUAL_UINT32(0, trace_ring_count(&r));
    TEST_ASSERT_NULL(trace_ring_at(&r, 0));
}

static void test_push_below_depth_orders_oldest_first(void)
{
    trace_ring_reset(&r, MAGIC);
    trace_ring_push(&r, 10, 1, 100, 200);
    trace_ring_push(&r, 20, 2, 300, 400);
    trace_ring_push(&r, 30, 3, 500, 600);

    TEST_ASSERT_EQUAL_UINT32(3, trace_ring_count(&r));
    TEST_ASSERT_EQUAL_UINT32(10, trace_ring_at(&r, 0)->t_ms);
    TEST_ASSERT_EQUAL_UINT32(30, trace_ring_at(&r, 2)->t_ms);
    TEST_ASSERT_EQUAL_INT32(500, trace_ring_at(&r, 2)->a);
    TEST_ASSERT_NULL(trace_ring_at(&r, 3));
}

static void test_push_past_depth_saturates_and_drops_oldest(void)
{
    trace_ring_reset(&r, MAGIC);
    for (uint32_t i = 0; i < TRACE_DEPTH + 5; i++) {
        trace_ring_push(&r, i, 1, (int32_t)i, 0);
    }
    TEST_ASSERT_EQUAL_UINT32(TRACE_DEPTH, trace_ring_count(&r));
    /* oldest surviving push is number 5 */
    TEST_ASSERT_EQUAL_INT32(5, trace_ring_at(&r, 0)->a);
    TEST_ASSERT_EQUAL_INT32(TRACE_DEPTH + 4,
                            trace_ring_at(&r, TRACE_DEPTH - 1)->a);
    /* A full ring must still pass validation; an off-by-one in the bounds check
     * would silently reject the most common real state after sustained logging. */
    TEST_ASSERT_TRUE(trace_ring_valid(&r, MAGIC));
}

/* A wrapped ring must still read oldest-first, not from index 0. */
static void test_wrapped_ring_reads_in_chronological_order(void)
{
    trace_ring_reset(&r, MAGIC);
    for (uint32_t i = 0; i < TRACE_DEPTH * 2; i++) {
        trace_ring_push(&r, i, 1, (int32_t)i, 0);
    }
    for (uint32_t i = 1; i < trace_ring_count(&r); i++) {
        TEST_ASSERT_TRUE(trace_ring_at(&r, i)->t_ms >
                         trace_ring_at(&r, i - 1)->t_ms);
    }
}

static void test_seq_is_monotonic_across_wrap(void)
{
    trace_ring_reset(&r, MAGIC);
    for (uint32_t i = 0; i < TRACE_DEPTH + 10; i++) {
        trace_ring_push(&r, i, 1, 0, 0);
    }
    uint16_t prev = trace_ring_at(&r, 0)->seq;
    for (uint32_t i = 1; i < trace_ring_count(&r); i++) {
        TEST_ASSERT_EQUAL_UINT16((uint16_t)(prev + 1), trace_ring_at(&r, i)->seq);
        prev = trace_ring_at(&r, i)->seq;
    }
}

static void test_reset_clears_count_but_keeps_magic(void)
{
    trace_ring_reset(&r, MAGIC);
    trace_ring_push(&r, 1, 1, 0, 0);
    trace_ring_reset(&r, MAGIC);
    TEST_ASSERT_EQUAL_UINT32(0, trace_ring_count(&r));
    TEST_ASSERT_TRUE(trace_ring_valid(&r, MAGIC));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_uninitialised_ring_is_not_valid);
    RUN_TEST(test_right_magic_but_bogus_indices_is_not_valid);
    RUN_TEST(test_reset_makes_valid_and_empty);
    RUN_TEST(test_push_below_depth_orders_oldest_first);
    RUN_TEST(test_push_past_depth_saturates_and_drops_oldest);
    RUN_TEST(test_wrapped_ring_reads_in_chronological_order);
    RUN_TEST(test_seq_is_monotonic_across_wrap);
    RUN_TEST(test_reset_clears_count_but_keeps_magic);
    return UNITY_END();
}
