#include <math.h>
#include <string.h>
#include "unity.h"
#include "el_ui_fmt.h"

static void test_fixed(void)
{
    char b[24];
    el_ui_fmt_fixed(24.56f, 1, "\xC2\xB0" "C", b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("24.6 \xC2\xB0" "C", b);
    el_ui_fmt_fixed(-0.04f, 1, "%", b, sizeof b);  TEST_ASSERT_EQUAL_STRING("0.0 %", b);  /* no -0.0 */
    el_ui_fmt_fixed(-1.25f, 2, NULL, b, sizeof b); TEST_ASSERT_EQUAL_STRING("-1.25", b);
    el_ui_fmt_fixed(NAN, 1, "C", b, sizeof b);     TEST_ASSERT_EQUAL_STRING(EL_UI_NA, b);
    el_ui_fmt_fixed(-18.25f, 1, NULL, b, sizeof b); TEST_ASSERT_EQUAL_STRING("-18.2", b);  /* freezer */
    el_ui_fmt_fixed(45.0f, 9, "%RH", b, sizeof b); TEST_ASSERT_EQUAL_STRING("45.000 %RH", b);  /* clamped to 3 */
}

static void test_durations(void)
{
    char b[24];
    el_ui_fmt_secs(0, b, sizeof b);                       TEST_ASSERT_EQUAL_STRING("0 s", b);
    el_ui_fmt_secs(45, b, sizeof b);                      TEST_ASSERT_EQUAL_STRING("45 s", b);
    el_ui_fmt_secs(300, b, sizeof b);                     TEST_ASSERT_EQUAL_STRING("5 min", b);
    el_ui_fmt_secs(320, b, sizeof b);                     TEST_ASSERT_EQUAL_STRING("5 min 20 s", b);
    el_ui_fmt_secs(7200, b, sizeof b);                    TEST_ASSERT_EQUAL_STRING("2 h", b);
    el_ui_fmt_secs(5400, b, sizeof b);                    TEST_ASSERT_EQUAL_STRING("1 h 30 min", b);
    el_ui_fmt_secs(86400, b, sizeof b);                   TEST_ASSERT_EQUAL_STRING("1 d", b);
    el_ui_fmt_secs(93600, b, sizeof b);                   TEST_ASSERT_EQUAL_STRING("1 d 2 h", b);
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_ui_fmt_secs(5400, b, 4));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_ui_fmt_secs(1, NULL, 4));
    el_ui_fmt_uptime(59, b, sizeof b);                    TEST_ASSERT_EQUAL_STRING("00:00:59", b);
    el_ui_fmt_uptime(3 * 86400 + 4 * 3600 + 12 * 60 + 9, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("3 d 04:12:09", b);
}

static void test_ago(void)
{
    char b[24];
    int64_t now = 1793361600;                              /* 2026-10-30 12:00Z */
    el_ui_fmt_ago(now, now - 30, b, sizeof b);      TEST_ASSERT_EQUAL_STRING("just now", b);
    el_ui_fmt_ago(now, now - 300, b, sizeof b);     TEST_ASSERT_EQUAL_STRING("5 min ago", b);
    el_ui_fmt_ago(now, now - 3 * 3600, b, sizeof b); TEST_ASSERT_EQUAL_STRING("3 h ago", b);
    el_ui_fmt_ago(now, now - 2 * 86400, b, sizeof b); TEST_ASSERT_EQUAL_STRING("2 d ago", b);
    el_ui_fmt_ago(now, now - 90 * 86400LL, b, sizeof b); TEST_ASSERT_EQUAL_STRING("> 60 d ago", b);
    el_ui_fmt_ago(now, now + 500, b, sizeof b);     TEST_ASSERT_EQUAL_STRING("just now", b);  /* clock set back */
    el_ui_fmt_ago(now, 0, b, sizeof b);             TEST_ASSERT_EQUAL_STRING(EL_UI_NA, b);
}

static void test_dates_with_offset_and_formats(void)
{
    char b[24];
    int64_t t = 1793361600 + 5 * 60 + 9;                   /* 2026-10-30 12:05:09Z */
    el_ui_fmt_date(t, 0, EL_DATE_ISO, b, sizeof b);   TEST_ASSERT_EQUAL_STRING("2026-10-30", b);
    el_ui_fmt_date(t, 0, EL_DATE_US, b, sizeof b);    TEST_ASSERT_EQUAL_STRING("10/30/2026", b);
    el_ui_fmt_date(t, 0, EL_DATE_EU, b, sizeof b);    TEST_ASSERT_EQUAL_STRING("30.10.2026", b);
    el_ui_fmt_date(t, 0, 99, b, sizeof b);            TEST_ASSERT_EQUAL_STRING("2026-10-30", b);
    el_ui_fmt_time(t, 0, true, b, sizeof b);          TEST_ASSERT_EQUAL_STRING("12:05:09", b);
    el_ui_fmt_time(t, 13 * 60, false, b, sizeof b);   TEST_ASSERT_EQUAL_STRING("01:05", b);  /* NZDT next day */
    el_ui_fmt_date(t, 13 * 60, EL_DATE_ISO, b, sizeof b); TEST_ASSERT_EQUAL_STRING("2026-10-31", b);
    el_ui_fmt_datetime(t, -300, EL_DATE_US, b, sizeof b); TEST_ASSERT_EQUAL_STRING("10/30/2026 07:05", b);
    el_ui_fmt_short(t, 0, b, sizeof b);               TEST_ASSERT_EQUAL_STRING("Oct 30 12:05", b);
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_ui_fmt_date(t, 0, EL_DATE_ISO, b, 6));
}

static void test_paging_newest_first(void)
{
    uint32_t start, n;
    TEST_ASSERT_EQUAL_UINT32(1, el_ui_page_count(0, 5));
    TEST_ASSERT_EQUAL_UINT32(1, el_ui_page_count(5, 5));
    TEST_ASSERT_EQUAL_UINT32(41, el_ui_page_count(201, 5));
    TEST_ASSERT_TRUE(el_ui_page_newest_first(201, 5, 0, &start, &n));
    TEST_ASSERT_EQUAL_UINT32(200, start); TEST_ASSERT_EQUAL_UINT32(5, n);
    TEST_ASSERT_TRUE(el_ui_page_newest_first(201, 5, 40, &start, &n));   /* last page: 1 row */
    TEST_ASSERT_EQUAL_UINT32(0, start); TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_FALSE(el_ui_page_newest_first(201, 5, 41, &start, &n));
    TEST_ASSERT_EQUAL_UINT32(0, n);
    TEST_ASSERT_FALSE(el_ui_page_newest_first(0, 5, 0, &start, &n));     /* empty log */
    TEST_ASSERT_FALSE(el_ui_page_newest_first(10, 0, 0, &start, &n));
}

void run_ui_fmt_tests(void)
{
    RUN_TEST(test_fixed);
    RUN_TEST(test_durations);
    RUN_TEST(test_ago);
    RUN_TEST(test_dates_with_offset_and_formats);
    RUN_TEST(test_paging_newest_first);
}
