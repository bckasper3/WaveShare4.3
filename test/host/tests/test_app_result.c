#include <math.h>
#include <string.h>
#include "unity.h"
#include "el_app_result.h"

static void test_id_validation(void)
{
    TEST_ASSERT_TRUE(el_app_id_valid("", 19));
    TEST_ASSERT_TRUE(el_app_id_valid("Server room rack 3", 19));
    TEST_ASSERT_TRUE(el_app_id_valid("1234567890123456789", 19));
    TEST_ASSERT_FALSE(el_app_id_valid("12345678901234567890", 19));
    TEST_ASSERT_FALSE(el_app_id_valid("tab\there", 19));
    TEST_ASSERT_FALSE(el_app_id_valid("caf\xc3\xa9", 19));
    TEST_ASSERT_FALSE(el_app_id_valid(NULL, 19));
}

static void test_env_from_settings_and_conditions(void)
{
    el_settings_t s; el_settings_defaults(&s);
    s.repeats = 5;
    s.temp_low_c = 2.0f; s.temp_high_c = 8.0f;          /* vaccine fridge */
    s.rh_low_pct = 10.0f; s.rh_high_pct = 90.0f;
    s.temp_offset_c = -0.3f; s.rh_offset_pct = 1.5f;
    el_app_conditions_t c = { true, true };
    el_meas_env_t env;
    TEST_ASSERT_EQUAL(EL_OK, el_app_meas_env(&env, &s, &c));
    TEST_ASSERT_EQUAL_UINT8(5, env.repeats);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, env.limits.t_low_c);
    TEST_ASSERT_EQUAL_FLOAT(8.0f, env.limits.t_high_c);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, env.limits.rh_low_pct);
    TEST_ASSERT_EQUAL_FLOAT(90.0f, env.limits.rh_high_pct);
    TEST_ASSERT_EQUAL_FLOAT(-0.3f, env.t_offset_c);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, env.rh_offset_pct);
    TEST_ASSERT_TRUE(env.export_busy);
    TEST_ASSERT_TRUE(env.log_full);
    TEST_ASSERT_FALSE(env.stuck_detect);
    TEST_ASSERT_EQUAL_UINT32(0, env.conversion_timeout_ms);
    TEST_ASSERT_EQUAL(EL_OK, el_app_meas_env(&env, &s, NULL));
    TEST_ASSERT_FALSE(env.export_busy);
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_app_meas_env(NULL, &s, NULL));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_app_meas_env(&env, NULL, NULL));
}

static void test_result_fill_keeps_measurement_fields(void)
{
    el_result_t r; memset(&r, 0, sizeof r);
    r.temp_c = 21.4f; r.rh_pct = 40.0f; r.repeats = 3; r.status = EL_RES_NORMAL; r.flags = EL_RES_FLAG_SIMULATED;
    el_app_result_meta_t m = { 1793534400, "Lab 2 shelf", "OPERATOR-LONG" };
    el_app_result_fill(&r, &m);
    TEST_ASSERT_EQUAL_INT64(1793534400, r.timestamp_s);
    TEST_ASSERT_EQUAL_STRING("Lab 2 shelf", r.tag);
    TEST_ASSERT_EQUAL_STRING("OPERATO", r.operator_id);   /* cut to the 7-char record field */
    TEST_ASSERT_EQUAL_FLOAT(21.4f, r.temp_c);
    TEST_ASSERT_EQUAL_UINT8(EL_RES_FLAG_SIMULATED, r.flags);
    el_app_result_meta_t none = { 5, NULL, NULL };
    el_app_result_fill(&r, &none);
    TEST_ASSERT_EQUAL_STRING("", r.tag);
    TEST_ASSERT_EQUAL_STRING("", r.operator_id);
    el_app_result_meta_t long_tag = { 5, "A location name that is too long", NULL };
    el_app_result_fill(&r, &long_tag);
    TEST_ASSERT_EQUAL_UINT32(EL_TAG_LEN - 1, strlen(r.tag));
}

static void test_log_retry_schedule_wraps_safely(void)
{
    el_app_retry_t r; el_app_retry_clear(&r);
    TEST_ASSERT_FALSE(el_app_retry_due(&r, 0));
    uint32_t t0 = 0xFFFFF000u;                       /* 4 s before the ms clock wraps */
    el_app_retry_start(&r, t0);
    TEST_ASSERT_FALSE(el_app_retry_due(&r, t0 + 4999u));
    TEST_ASSERT_TRUE(el_app_retry_due(&r, t0 + 5000u));   /* wrapped past 0 */
    for (unsigned i = 0; i < EL_APP_LOG_RETRIES; i++) {
        TEST_ASSERT_FALSE(el_app_retry_exhausted(&r));
        el_app_retry_failed(&r, t0 + 5000u * (i + 1u));
    }
    TEST_ASSERT_TRUE(el_app_retry_exhausted(&r));
    TEST_ASSERT_FALSE(el_app_retry_due(&r, t0 + 100000u));
    el_app_retry_clear(&r);
    TEST_ASSERT_FALSE(el_app_retry_exhausted(&r));
}

void run_app_result_tests(void)
{
    RUN_TEST(test_id_validation);
    RUN_TEST(test_env_from_settings_and_conditions);
    RUN_TEST(test_result_fill_keeps_measurement_fields);
    RUN_TEST(test_log_retry_schedule_wraps_safely);
}
