#include <math.h>
#include <string.h>
#include "unity.h"
#include "el_stats.h"

static const el_limits_t L = { 18.0f, 26.0f, 30.0f, 60.0f };   /* office comfort band */

static void test_stats_three_readings(void)
{
    const float v[] = { 21.0f, 21.2f, 21.4f };
    el_stats_t s;
    TEST_ASSERT_EQUAL(EL_OK, el_stats_compute(v, 3, &s));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 21.2f, s.mean);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.2f, s.sd);
    TEST_ASSERT_EQUAL_FLOAT(21.0f, s.min);
    TEST_ASSERT_EQUAL_FLOAT(21.4f, s.max);
}

static void test_stats_single_and_negative_values(void)
{
    const float one[] = { -18.5f };
    el_stats_t s;
    TEST_ASSERT_EQUAL(EL_OK, el_stats_compute(one, 1, &s));
    TEST_ASSERT_EQUAL_FLOAT(-18.5f, s.mean);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.sd);
    const float v[] = { -1.0f, 1.0f };
    TEST_ASSERT_EQUAL(EL_OK, el_stats_compute(v, 2, &s));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.mean);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.41421f, s.sd);
}

static void test_stats_rejects_bad_input(void)
{
    const float v[] = { 1.0f, NAN };
    el_stats_t s = { 7.0f, 7.0f, 7.0f, 7.0f };
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_stats_compute(v, 2, &s));
    TEST_ASSERT_EQUAL_FLOAT(7.0f, s.mean);                      /* untouched */
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_stats_compute(v, 0, &s));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_stats_compute(NULL, 1, &s));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_stats_compute(v, 1, NULL));
}

static void test_classify_inside_and_boundaries(void)
{
    uint8_t a = 0xFF;
    TEST_ASSERT_EQUAL(EL_RES_NORMAL, el_classify(&L, 22.0f, 45.0f, &a));
    TEST_ASSERT_EQUAL_UINT8(0, a);
    /* a value equal to a limit is inside */
    TEST_ASSERT_EQUAL(EL_RES_NORMAL, el_classify(&L, 18.0f, 30.0f, &a));
    TEST_ASSERT_EQUAL(EL_RES_NORMAL, el_classify(&L, 26.0f, 60.0f, &a));
    TEST_ASSERT_EQUAL_UINT8(0, a);
}

static void test_classify_each_alarm_bit(void)
{
    uint8_t a;
    TEST_ASSERT_EQUAL(EL_RES_ALARM, el_classify(&L, 17.9f, 45.0f, &a));
    TEST_ASSERT_EQUAL_UINT8(EL_ALARM_T_LOW, a);
    TEST_ASSERT_EQUAL(EL_RES_ALARM, el_classify(&L, 26.1f, 45.0f, &a));
    TEST_ASSERT_EQUAL_UINT8(EL_ALARM_T_HIGH, a);
    TEST_ASSERT_EQUAL(EL_RES_ALARM, el_classify(&L, 22.0f, 29.9f, &a));
    TEST_ASSERT_EQUAL_UINT8(EL_ALARM_RH_LOW, a);
    TEST_ASSERT_EQUAL(EL_RES_ALARM, el_classify(&L, 22.0f, 60.5f, &a));
    TEST_ASSERT_EQUAL_UINT8(EL_ALARM_RH_HIGH, a);
    TEST_ASSERT_EQUAL(EL_RES_ALARM, el_classify(&L, 30.0f, 75.0f, &a));   /* hot and humid */
    TEST_ASSERT_EQUAL_UINT8(EL_ALARM_T_HIGH | EL_ALARM_RH_HIGH, a);
    TEST_ASSERT_EQUAL(EL_RES_ALARM, el_classify(&L, 30.0f, 75.0f, NULL));
}

static void test_classify_disabled_limits_and_errors(void)
{
    const el_limits_t off = { EL_TEMP_MIN_C, EL_TEMP_MAX_C, EL_RH_MIN_PCT, EL_RH_MAX_PCT };
    uint8_t a;
    TEST_ASSERT_EQUAL(EL_RES_NORMAL, el_classify(&off, -40.0f, 0.0f, &a));
    TEST_ASSERT_EQUAL(EL_RES_NORMAL, el_classify(&off, 125.0f, 100.0f, &a));
    TEST_ASSERT_EQUAL(EL_RES_NORMAL, el_classify(NULL, 99.0f, 99.0f, &a));
    TEST_ASSERT_EQUAL(EL_RES_ERROR, el_classify(&L, NAN, 40.0f, &a));
    TEST_ASSERT_EQUAL_UINT8(0, a);
    TEST_ASSERT_EQUAL(EL_RES_ERROR, el_classify(&L, 20.0f, INFINITY, &a));
}

static void test_limits_valid(void)
{
    TEST_ASSERT_TRUE(el_limits_valid(&L));
    el_limits_t l = L;
    l.t_low_c = 26.0f;                                   /* low == high */
    TEST_ASSERT_FALSE(el_limits_valid(&l));
    l = L; l.rh_high_pct = 100.1f;
    TEST_ASSERT_FALSE(el_limits_valid(&l));
    l = L; l.t_low_c = -41.0f;
    TEST_ASSERT_FALSE(el_limits_valid(&l));
    l = L; l.rh_low_pct = NAN;
    TEST_ASSERT_FALSE(el_limits_valid(&l));
    TEST_ASSERT_FALSE(el_limits_valid(NULL));
}

static void test_dew_point_reference_values(void)
{
    /* Magnus (17.62 / 243.12): reference values from the Sensirion application note */
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 13.86f, el_dew_point_c(25.0f, 50.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 20.0f, el_dew_point_c(20.0f, 100.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -9.2f, el_dew_point_c(0.0f, 50.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 20.0f, el_dew_point_c(20.0f, 105.0f));   /* > 100 clamped */
    TEST_ASSERT_TRUE(isnan(el_dew_point_c(20.0f, 0.0f)));
    TEST_ASSERT_TRUE(isnan(el_dew_point_c(NAN, 50.0f)));
    TEST_ASSERT_TRUE(isnan(el_dew_point_c(20.0f, NAN)));
}

static void test_alarm_words(void)
{
    char b[40];
    TEST_ASSERT_EQUAL(EL_OK, el_alarms_format(0, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("", b);
    TEST_ASSERT_EQUAL(EL_OK, el_alarms_format(EL_ALARM_T_LOW | EL_ALARM_RH_HIGH, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("T_LOW|RH_HIGH", b);
    TEST_ASSERT_EQUAL(EL_OK, el_alarms_format(0x0F, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("T_LOW|T_HIGH|RH_LOW|RH_HIGH", b);
    char s[8];
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_alarms_format(0x0F, s, sizeof s));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_alarms_format(1, NULL, 4));
}

void run_stats_tests(void)
{
    RUN_TEST(test_stats_three_readings);
    RUN_TEST(test_stats_single_and_negative_values);
    RUN_TEST(test_stats_rejects_bad_input);
    RUN_TEST(test_classify_inside_and_boundaries);
    RUN_TEST(test_classify_each_alarm_bit);
    RUN_TEST(test_classify_disabled_limits_and_errors);
    RUN_TEST(test_limits_valid);
    RUN_TEST(test_dew_point_reference_values);
    RUN_TEST(test_alarm_words);
}
