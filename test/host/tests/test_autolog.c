#include <stdint.h>
#include "unity.h"
#include "el_autolog.h"

static void test_interval_range(void)
{
    TEST_ASSERT_TRUE(el_autolog_interval_ok(0));
    TEST_ASSERT_FALSE(el_autolog_interval_ok(9));
    TEST_ASSERT_TRUE(el_autolog_interval_ok(10));
    TEST_ASSERT_TRUE(el_autolog_interval_ok(86400));
    TEST_ASSERT_FALSE(el_autolog_interval_ok(86401));
}

static void test_off_never_due(void)
{
    el_autolog_t a;
    el_autolog_init(&a, 0, 1000);
    TEST_ASSERT_FALSE(el_autolog_due(&a, 1000));
    TEST_ASSERT_FALSE(el_autolog_due(&a, 0xFFFFFFFFu));
    TEST_ASSERT_EQUAL_HEX32(UINT32_MAX, el_autolog_remaining_s(&a, 5000));
    el_autolog_taken(&a, 5000);                          /* harmless */
    el_autolog_init(&a, 5, 0);                           /* invalid -> off */
    TEST_ASSERT_EQUAL_UINT32(0, a.interval_s);
    TEST_ASSERT_FALSE(el_autolog_due(NULL, 0));
}

static void test_first_slot_after_one_interval_then_fixed_grid(void)
{
    el_autolog_t a;
    el_autolog_init(&a, 60, 10000);                      /* 1 min logging from t = 10 s */
    TEST_ASSERT_FALSE(el_autolog_due(&a, 69999));
    TEST_ASSERT_EQUAL_UINT32(1, el_autolog_remaining_s(&a, 69001));
    TEST_ASSERT_TRUE(el_autolog_due(&a, 70000));
    TEST_ASSERT_EQUAL_UINT32(0, el_autolog_remaining_s(&a, 70000));
    el_autolog_taken(&a, 70300);                         /* measurement started 300 ms late */
    TEST_ASSERT_FALSE(el_autolog_due(&a, 129999));       /* grid: next slot at 130 s, no drift */
    TEST_ASSERT_TRUE(el_autolog_due(&a, 130000));
    TEST_ASSERT_EQUAL_UINT32(60, el_autolog_remaining_s(&a, 70000));
}

static void test_missed_slots_are_skipped_not_caught_up(void)
{
    el_autolog_t a;
    el_autolog_init(&a, 10, 0);
    /* busy for 35 s (e.g. an export): slots at 10, 20, 30 s missed */
    TEST_ASSERT_TRUE(el_autolog_due(&a, 35000));
    el_autolog_taken(&a, 35000);
    TEST_ASSERT_FALSE(el_autolog_due(&a, 35001));        /* one reading, not three */
    TEST_ASSERT_TRUE(el_autolog_due(&a, 40000));
    TEST_ASSERT_EQUAL_UINT32(5, el_autolog_remaining_s(&a, 35000));
}

static void test_change_interval_restarts_only_on_change(void)
{
    el_autolog_t a;
    el_autolog_init(&a, 300, 0);
    el_autolog_set_interval(&a, 300, 200000);            /* same value: grid kept */
    TEST_ASSERT_TRUE(el_autolog_due(&a, 300000));
    el_autolog_set_interval(&a, 30, 250000);             /* changed: next at 280 s */
    TEST_ASSERT_FALSE(el_autolog_due(&a, 279999));
    TEST_ASSERT_TRUE(el_autolog_due(&a, 280000));
    el_autolog_set_interval(&a, 0, 280000);
    TEST_ASSERT_FALSE(el_autolog_due(&a, 999999));
}

static void test_ms_clock_wrap_and_one_day(void)
{
    el_autolog_t a;
    uint32_t t0 = 0xFFFFFF00u;                           /* 256 ms before the clock wraps */
    el_autolog_init(&a, 10, t0);
    TEST_ASSERT_FALSE(el_autolog_due(&a, t0 + 9999u));
    TEST_ASSERT_TRUE(el_autolog_due(&a, t0 + 10000u));   /* wrapped past 0 */
    el_autolog_init(&a, EL_AUTOLOG_MAX_S, 5);
    TEST_ASSERT_FALSE(el_autolog_due(&a, 5u + 86399999u));
    TEST_ASSERT_TRUE(el_autolog_due(&a, 5u + 86400000u));
    TEST_ASSERT_EQUAL_UINT32(86400, el_autolog_remaining_s(&a, 5));
}

void run_autolog_tests(void)
{
    RUN_TEST(test_interval_range);
    RUN_TEST(test_off_never_due);
    RUN_TEST(test_first_slot_after_one_interval_then_fixed_grid);
    RUN_TEST(test_missed_slots_are_skipped_not_caught_up);
    RUN_TEST(test_change_interval_restarts_only_on_change);
    RUN_TEST(test_ms_clock_wrap_and_one_day);
}
