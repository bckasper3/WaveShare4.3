#include <string.h>
#include "unity.h"
#include "el_time.h"

static void test_epoch_and_known_dates(void)
{
    char b[24];
    TEST_ASSERT_EQUAL(EL_OK, el_time_format_iso8601(0, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("1970-01-01T00:00:00Z", b);
    el_time_format_iso8601(946684800, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("2000-01-01T00:00:00Z", b);
    el_time_format_iso8601(1709251199, b, sizeof b);            /* leap day, last second */
    TEST_ASSERT_EQUAL_STRING("2024-02-29T23:59:59Z", b);
    el_time_format_compact(1709251200, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("20240301-000000", b);
}

static void test_round_trip_every_day_1970_to_2100(void)
{
    for (int64_t t = 0; t < 4102444800LL; t += 86400 + 3601) {   /* drifts through all hours */
        el_datetime_t dt; int64_t back;
        el_time_to_datetime(t, &dt);
        TEST_ASSERT_EQUAL(EL_OK, el_datetime_to_time(&dt, &back));
        TEST_ASSERT_EQUAL_INT64(t, back);
    }
}

static void test_parse_valid(void)
{
    int64_t t;
    TEST_ASSERT_EQUAL(EL_OK, el_time_parse_iso8601("2024-02-29T23:59:59Z", &t));
    TEST_ASSERT_EQUAL_INT64(1709251199, t);
    TEST_ASSERT_EQUAL(EL_OK, el_time_parse_iso8601("2024-02-29 23:59:59", &t));
    TEST_ASSERT_EQUAL_INT64(1709251199, t);
}

static void test_parse_rejects_invalid(void)
{
    int64_t t = 7;
    TEST_ASSERT_EQUAL(EL_ERR_RANGE,  el_time_parse_iso8601("2026-13-01T00:00:00Z", &t));
    TEST_ASSERT_EQUAL(EL_ERR_RANGE,  el_time_parse_iso8601("2026-02-29T00:00:00Z", &t));  /* not leap */
    TEST_ASSERT_EQUAL(EL_ERR_RANGE,  el_time_parse_iso8601("2026-10-30T24:00:00Z", &t));
    TEST_ASSERT_EQUAL(EL_ERR_RANGE,  el_time_parse_iso8601("1969-12-31T23:59:59Z", &t));
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_time_parse_iso8601("2026-10-30T12:00:00Zjunk", &t));
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_time_parse_iso8601("2026-10-30T12:00", &t));
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_time_parse_iso8601("26-10-30T12:00:00Z", &t));
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_time_parse_iso8601("", &t));
    TEST_ASSERT_EQUAL_INT64(7, t);
}

static void test_format_buffer_too_small(void)
{
    char b[20];
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_time_format_iso8601(0, b, sizeof b));
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_time_format_compact(0, b, 15));
}

void run_time_tests(void)
{
    RUN_TEST(test_epoch_and_known_dates);
    RUN_TEST(test_round_trip_every_day_1970_to_2100);
    RUN_TEST(test_parse_valid);
    RUN_TEST(test_parse_rejects_invalid);
    RUN_TEST(test_format_buffer_too_small);
}
