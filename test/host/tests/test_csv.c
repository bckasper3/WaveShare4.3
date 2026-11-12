#include <math.h>
#include <string.h>
#include "unity.h"
#include "el_csv.h"
#include "el_stats.h"
#include "el_util.h"

static const el_csv_ctx_t CTX = { "EL-0001", "0.1.0", 1 };

static el_result_t example(void)
{
    el_result_t r = {0};
    r.seq = 42;
    r.timestamp_s = 1793361600;                     /* 2026-10-30T12:00:00Z */
    el_strlcpy(r.tag, "Greenhouse 2", sizeof r.tag);
    el_strlcpy(r.operator_id, "JD", sizeof r.operator_id);
    r.temp_c = 24.6f; r.temp_sd_c = 0.02f; r.rh_pct = 65.25f; r.rh_sd_pct = 0.15f; r.repeats = 3;
    r.dew_point_c = el_dew_point_c(r.temp_c, r.rh_pct);
    r.status = EL_RES_NORMAL;
    r.limits.t_low_c = 5.0f; r.limits.t_high_c = 35.0f;
    r.limits.rh_low_pct = 20.0f; r.limits.rh_high_pct = 80.0f;
    return r;
}

static int count_commas(const char *s)
{
    int n = 0;
    for (; *s; s++) n += *s == ',';
    return n;
}

static void test_escape_plain_and_quoted(void)
{
    char b[64];
    TEST_ASSERT_EQUAL(EL_OK, el_csv_escape(b, sizeof b, "Lab-1"));
    TEST_ASSERT_EQUAL_STRING("Lab-1", b);
    el_csv_escape(b, sizeof b, "Hall 3, north");
    TEST_ASSERT_EQUAL_STRING("\"Hall 3, north\"", b);
    el_csv_escape(b, sizeof b, "the \"big\" one");
    TEST_ASSERT_EQUAL_STRING("\"the \"\"big\"\" one\"", b);
    el_csv_escape(b, sizeof b, "two\nlines");
    TEST_ASSERT_EQUAL_STRING("\"two\nlines\"", b);
    el_csv_escape(b, sizeof b, "");
    TEST_ASSERT_EQUAL_STRING("", b);
}

static void test_escape_formula_injection(void)
{
    char b[64];
    el_csv_escape(b, sizeof b, "=HYPERLINK(\"x\")");
    TEST_ASSERT_EQUAL_STRING("\"'=HYPERLINK(\"\"x\"\")\"", b);
    el_csv_escape(b, sizeof b, "+1");
    TEST_ASSERT_EQUAL_STRING("'+1", b);
    el_csv_escape(b, sizeof b, "@SUM");
    TEST_ASSERT_EQUAL_STRING("'@SUM", b);
    el_csv_escape(b, sizeof b, "A-1");                /* dash later in the text is fine */
    TEST_ASSERT_EQUAL_STRING("A-1", b);
}

static void test_escape_too_small(void)
{
    char b[6];
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_csv_escape(b, sizeof b, "a,b,c,d"));
    TEST_ASSERT_EQUAL('\0', b[sizeof b - 1]);         /* still terminated */
}

static void test_format_example_row(void)
{
    char row[EL_CSV_ROW_MAX];
    el_result_t r = example();
    TEST_ASSERT_EQUAL(EL_OK, el_csv_format_result(row, sizeof row, &r, &CTX));
    TEST_ASSERT_EQUAL_STRING(
        "2026-10-30T12:00:00Z,Greenhouse 2,JD,24.6,0.02,65.2,0.15,17.6,3,NORMAL,,"
        "5.0,35.0,20.0,80.0,EL-0001,0.1.0,42,", row);
    TEST_ASSERT_EQUAL(count_commas(el_csv_header()), count_commas(row));
}

static void test_format_alarms_flags_and_missing_values(void)
{
    char row[EL_CSV_ROW_MAX];
    el_result_t r = example();
    r.dew_point_c = NAN;
    r.status = EL_RES_ALARM;
    r.alarms = EL_ALARM_T_HIGH | EL_ALARM_RH_LOW;
    r.flags = EL_RES_FLAG_SIMULATED | EL_RES_FLAG_AUTO | EL_RES_FLAG_ADJUSTED;
    TEST_ASSERT_EQUAL(EL_OK, el_csv_format_result(row, sizeof row, &r, &CTX));
    TEST_ASSERT_NOT_NULL(strstr(row, ",0.15,,3,ALARM,T_HIGH|RH_LOW,5.0,"));
    TEST_ASSERT_NOT_NULL(strstr(row, ",42,SIM|AUTO|ADJ"));
    TEST_ASSERT_NULL(strstr(row, "nan"));
}

static void test_format_negative_temperature_and_decimals(void)
{
    char row[EL_CSV_ROW_MAX];
    el_result_t r = example();
    r.temp_c = -18.25f;                               /* freezer */
    el_csv_ctx_t c2 = CTX;
    c2.decimals = 2;
    TEST_ASSERT_EQUAL(EL_OK, el_csv_format_result(row, sizeof row, &r, &c2));
    TEST_ASSERT_NOT_NULL(strstr(row, ",JD,-18.25,0.020,65.25,"));
    c2.decimals = 9;                                  /* clamped to 3 */
    TEST_ASSERT_EQUAL(EL_OK, el_csv_format_result(row, sizeof row, &r, &c2));
    TEST_ASSERT_NOT_NULL(strstr(row, ",-18.250,"));
}

static void test_format_worst_case_fits_row_max(void)
{
    char row[EL_CSV_ROW_MAX];
    el_result_t r = example();
    /* every text field full of quotes doubles in size: still must fit */
    memset(r.tag, '"', sizeof r.tag - 1);
    memset(r.operator_id, '"', sizeof r.operator_id - 1);
    r.temp_c = -1e30f; r.temp_sd_c = 1e30f; r.rh_sd_pct = 1e30f; r.dew_point_c = -1e30f;
    r.alarms = 0x0F;
    r.flags = 0xFF;
    TEST_ASSERT_EQUAL(EL_OK, el_csv_format_result(row, sizeof row, &r, &CTX));
}

static void test_format_too_small(void)
{
    char row[40];
    el_result_t r = example();
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_csv_format_result(row, sizeof row, &r, &CTX));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_csv_format_result(row, sizeof row, NULL, &CTX));
}

void run_csv_tests(void)
{
    RUN_TEST(test_escape_plain_and_quoted);
    RUN_TEST(test_escape_formula_injection);
    RUN_TEST(test_escape_too_small);
    RUN_TEST(test_format_example_row);
    RUN_TEST(test_format_alarms_flags_and_missing_values);
    RUN_TEST(test_format_negative_temperature_and_decimals);
    RUN_TEST(test_format_worst_case_fits_row_max);
    RUN_TEST(test_format_too_small);
}
