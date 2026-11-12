#include <math.h>
#include <string.h>
#include "unity.h"
#include "el_uplink.h"
#include "el_settings.h"

static const el_uplink_device_t DEV = { "EL-00017", "Archive B", "0.1.0", 1 };

static el_result_t sample(uint32_t seq)
{
    el_result_t r;
    memset(&r, 0, sizeof r);
    r.seq = seq;
    r.timestamp_s = 1793630000;                   /* 2026-11-02T... */
    strcpy(r.tag, "Archive B");
    strcpy(r.operator_id, "JD");
    r.temp_c = 21.44f;
    r.temp_sd_c = 0.034f;
    r.rh_pct = 48.56f;
    r.rh_sd_pct = 0.123f;
    r.dew_point_c = 10.06f;
    r.repeats = 3;
    r.status = EL_RES_NORMAL;
    r.limits.t_low_c = 15.0f; r.limits.t_high_c = 25.0f;
    r.limits.rh_low_pct = 35.0f; r.limits.rh_high_pct = 55.0f;
    return r;
}

static void test_url_rules(void)
{
    TEST_ASSERT_TRUE(el_uplink_url_ok(""));
    TEST_ASSERT_TRUE(el_uplink_url_ok("https://api.example.com/env"));
    TEST_ASSERT_TRUE(el_uplink_url_ok("http://192.168.1.20:8080/in?site=7"));
    TEST_ASSERT_FALSE(el_uplink_url_ok("ftp://example.com"));
    TEST_ASSERT_FALSE(el_uplink_url_ok("example.com/x"));
    TEST_ASSERT_FALSE(el_uplink_url_ok("https://"));
    TEST_ASSERT_FALSE(el_uplink_url_ok("https:///path"));
    TEST_ASSERT_FALSE(el_uplink_url_ok("https://user:pw@example.com/"));
    TEST_ASSERT_FALSE(el_uplink_url_ok("https://exa mple.com"));
    TEST_ASSERT_FALSE(el_uplink_url_ok("https://example.com/\x01"));
    TEST_ASSERT_FALSE(el_uplink_url_ok(NULL));
    char long_url[EL_UPLINK_URL_MAX + 2];
    memset(long_url, 'a', sizeof long_url);
    memcpy(long_url, "https://", 8);
    long_url[EL_UPLINK_URL_MAX] = '\0';
    TEST_ASSERT_TRUE(el_uplink_url_ok(long_url));
    long_url[EL_UPLINK_URL_MAX] = 'a';
    long_url[EL_UPLINK_URL_MAX + 1] = '\0';
    TEST_ASSERT_FALSE(el_uplink_url_ok(long_url));
    TEST_ASSERT_TRUE(el_uplink_url_is_https("https://a.b"));
    TEST_ASSERT_FALSE(el_uplink_url_is_https("http://a.b"));
    char host[40];
    TEST_ASSERT_EQUAL(EL_OK, el_uplink_url_host("https://api.example.com:443/env", host, sizeof host));
    TEST_ASSERT_EQUAL_STRING("api.example.com", host);
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_uplink_url_host("nope", host, sizeof host));
    TEST_ASSERT_EQUAL_STRING("", host);
}

static void test_credentials(void)
{
    TEST_ASSERT_TRUE(el_uplink_ssid_ok(""));
    TEST_ASSERT_TRUE(el_uplink_ssid_ok("Plant Floor 2.4"));
    TEST_ASSERT_FALSE(el_uplink_ssid_ok("123456789012345678901234567890123"));   /* 33 */
    TEST_ASSERT_TRUE(el_uplink_password_ok(""));
    TEST_ASSERT_FALSE(el_uplink_password_ok("1234567"));
    TEST_ASSERT_TRUE(el_uplink_password_ok("12345678"));
    TEST_ASSERT_FALSE(el_uplink_password_ok("pass\tword"));
}

static void test_json_escape(void)
{
    char b[64];
    TEST_ASSERT_EQUAL(EL_OK, el_json_escape(b, sizeof b, "a\"b\\c\nd\x01"));
    TEST_ASSERT_EQUAL_STRING("a\\\"b\\\\c\\nd\\u0001", b);
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_json_escape(b, 4, "abcd"));
    TEST_ASSERT_EQUAL_STRING("", b);
}

static void test_body_one_result(void)
{
    char buf[EL_UPLINK_BODY_MAX];
    el_uplink_body_t b;
    el_uplink_body_begin(&b, buf, sizeof buf, &DEV, false);
    el_result_t r = sample(1043);
    r.status = EL_RES_ALARM;
    r.alarms = EL_ALARM_T_HIGH | EL_ALARM_RH_LOW;
    r.flags = EL_RES_FLAG_AUTO | EL_RES_FLAG_ADJUSTED;
    TEST_ASSERT_EQUAL(EL_OK, el_uplink_body_add(&b, &r, &DEV));
    size_t n = 0;
    TEST_ASSERT_EQUAL(EL_OK, el_uplink_body_end(&b, &n));
    TEST_ASSERT_EQUAL(strlen(buf), n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "{\"device\":{\"type\":\"envlogger\",\"serial\":\"EL-00017\",\"location\":\"Archive B\",\"fw\":\"0.1.0\"}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"test\":false,\"results\":[{\"record\":1043,"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"tag\":\"Archive B\",\"operator\":\"JD\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"temperature_c\":21.4,"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"temperature_sd_c\":0.03,"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"humidity_pct\":48.6,"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"dew_point_c\":10.1,"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"status\":\"ALARM\",\"alarms\":\"T_HIGH|RH_LOW\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"limit_t_low_c\":15.0,\"limit_t_high_c\":25.0,\"limit_rh_low_pct\":35.0,\"limit_rh_high_pct\":55.0"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"flags\":[\"AUTO\",\"ADJ\"]}]}"));
    TEST_ASSERT_EQUAL_CHAR('}', buf[n - 1]);
}

static void test_body_nulls_and_test_message(void)
{
    char buf[1024];
    el_uplink_body_t b;
    el_uplink_body_begin(&b, buf, sizeof buf, &DEV, true);
    size_t n;
    TEST_ASSERT_EQUAL(EL_OK, el_uplink_body_end(&b, &n));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"test\":true,\"results\":[]}"));

    el_uplink_body_begin(&b, buf, sizeof buf, &DEV, false);
    el_result_t r = sample(1);
    r.dew_point_c = NAN;
    TEST_ASSERT_EQUAL(EL_OK, el_uplink_body_add(&b, &r, &DEV));
    TEST_ASSERT_EQUAL(EL_OK, el_uplink_body_end(&b, &n));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"dew_point_c\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"alarms\":\"\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"flags\":[]"));
}

static void test_body_batch_limits_and_rollback(void)
{
    char buf[EL_UPLINK_BODY_MAX];
    el_uplink_body_t b;
    el_uplink_body_begin(&b, buf, sizeof buf, &DEV, false);
    for (uint32_t i = 0; i < EL_UPLINK_BATCH_MAX; i++) {
        el_result_t r = sample(i + 1);
        memset(r.tag, '"', EL_TAG_LEN - 1);                   /* worst-case escaping */
        r.tag[EL_TAG_LEN - 1] = '\0';
        memset(r.operator_id, '\x01', EL_OPERATOR_LEN - 1);  /* \u0001 each */
        r.temp_c = -1e30f; r.rh_sd_pct = 1e30f;
        TEST_ASSERT_EQUAL(EL_OK, el_uplink_body_add(&b, &r, &DEV));
    }
    el_result_t extra = sample(99);
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_uplink_body_add(&b, &extra, &DEV));
    size_t n;
    TEST_ASSERT_EQUAL(EL_OK, el_uplink_body_end(&b, &n));

    /* a small buffer: the second result does not fit and is rolled back, the body stays valid */
    char small[700];
    el_uplink_body_begin(&b, small, sizeof small, &DEV, false);
    el_result_t r1 = sample(1), r2 = sample(2);
    TEST_ASSERT_EQUAL(EL_OK, el_uplink_body_add(&b, &r1, &DEV));
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_uplink_body_add(&b, &r2, &DEV));
    TEST_ASSERT_EQUAL(1, b.n);
    TEST_ASSERT_EQUAL(EL_OK, el_uplink_body_end(&b, &n));
    TEST_ASSERT_NULL(strstr(small, "\"record\":2"));
    TEST_ASSERT_EQUAL_STRING("]}", small + n - 2);
}

static void test_classify(void)
{
    TEST_ASSERT_EQUAL(EL_UPLINK_SENT, el_uplink_classify(200));
    TEST_ASSERT_EQUAL(EL_UPLINK_SENT, el_uplink_classify(204));
    TEST_ASSERT_EQUAL(EL_UPLINK_RETRY, el_uplink_classify(-1));
    TEST_ASSERT_EQUAL(EL_UPLINK_RETRY, el_uplink_classify(0));
    TEST_ASSERT_EQUAL(EL_UPLINK_RETRY, el_uplink_classify(408));
    TEST_ASSERT_EQUAL(EL_UPLINK_RETRY, el_uplink_classify(429));
    TEST_ASSERT_EQUAL(EL_UPLINK_RETRY, el_uplink_classify(503));
    TEST_ASSERT_EQUAL(EL_UPLINK_REJECTED, el_uplink_classify(401));
    TEST_ASSERT_EQUAL(EL_UPLINK_REJECTED, el_uplink_classify(404));
    TEST_ASSERT_EQUAL(EL_UPLINK_REJECTED, el_uplink_classify(301));   /* redirects are not followed */
    TEST_ASSERT_EQUAL_STRING("REJECTED", el_uplink_verdict_name(EL_UPLINK_REJECTED));
}

static void test_backoff_and_schedule(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, el_uplink_backoff_ms(0));
    TEST_ASSERT_EQUAL_UINT32(5000, el_uplink_backoff_ms(1));
    TEST_ASSERT_EQUAL_UINT32(10000, el_uplink_backoff_ms(2));
    TEST_ASSERT_EQUAL_UINT32(EL_UPLINK_BACKOFF_MAX_MS, el_uplink_backoff_ms(20));
    TEST_ASSERT_EQUAL_UINT32(EL_UPLINK_BACKOFF_MAX_MS, el_uplink_backoff_ms(UINT16_MAX));

    el_uplink_sched_t s;
    el_uplink_sched_reset(&s);
    TEST_ASSERT_TRUE(el_uplink_sched_due(&s, 123));
    el_uplink_sched_done(&s, EL_UPLINK_RETRY, 1000);
    TEST_ASSERT_FALSE(el_uplink_sched_due(&s, 5999));
    TEST_ASSERT_EQUAL_UINT32(1, el_uplink_sched_wait_ms(&s, 5999));
    TEST_ASSERT_TRUE(el_uplink_sched_due(&s, 6000));
    el_uplink_sched_done(&s, EL_UPLINK_RETRY, 6000);
    TEST_ASSERT_EQUAL_UINT32(10000, el_uplink_sched_wait_ms(&s, 6000));
    el_uplink_sched_done(&s, EL_UPLINK_SENT, 20000);
    TEST_ASSERT_TRUE(el_uplink_sched_due(&s, 20000));
    TEST_ASSERT_EQUAL_UINT16(0, s.failures);
    el_uplink_sched_done(&s, EL_UPLINK_REJECTED, 0xFFFFFF00u);                  /* across the wrap */
    TEST_ASSERT_FALSE(el_uplink_sched_due(&s, 0x00001000u));
    TEST_ASSERT_TRUE(el_uplink_sched_due(&s, 0xFFFFFF00u + EL_UPLINK_REJECT_WAIT_MS));
}

static void test_settings_wifi(void)
{
    el_settings_t s;
    el_settings_defaults(&s);
    TEST_ASSERT_FALSE(s.wifi_enabled);                                 /* ships off */
    TEST_ASSERT_EQUAL_STRING("", s.upload_url);
    TEST_ASSERT_EQUAL(EL_SET_ERR_DENIED, el_settings_set_str(&s, EL_SET_WIFI_ENABLED, "1", EL_LEVEL_OPERATOR));
    TEST_ASSERT_EQUAL(EL_OK, el_settings_set_str(&s, EL_SET_WIFI_ENABLED, "1", EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_set_str(&s, EL_SET_UPLOAD_URL, "ftp://x", EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL(EL_OK, el_settings_set_str(&s, EL_SET_UPLOAD_URL, "https://api.example.com/env", EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_set_str(&s, EL_SET_WIFI_PASSWORD, "short", EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL(EL_OK, el_settings_set_str(&s, EL_SET_WIFI_PASSWORD, "correct horse", EL_LEVEL_ADMIN));
    char v[32];
    TEST_ASSERT_EQUAL(EL_OK, el_settings_get_str(&s, EL_SET_WIFI_PASSWORD, v, sizeof v));
    TEST_ASSERT_EQUAL_STRING("****", v);                               /* secret never printed */

    /* worst case: every string full still encodes */
    memset(s.wifi_ssid, 'S', EL_UPLINK_SSID_MAX); s.wifi_ssid[EL_UPLINK_SSID_MAX] = '\0';
    memset(s.wifi_password, 'P', EL_UPLINK_PASS_MAX); s.wifi_password[EL_UPLINK_PASS_MAX] = '\0';
    memset(s.upload_key, 'K', EL_UPLINK_TOKEN_MAX); s.upload_key[EL_UPLINK_TOKEN_MAX] = '\0';
    memset(s.upload_url, 'u', EL_UPLINK_URL_MAX); memcpy(s.upload_url, "https://", 8);
    s.upload_url[EL_UPLINK_URL_MAX] = '\0';
    uint8_t buf[EL_SETTINGS_ENCODED_MAX];
    size_t n = 0;
    TEST_ASSERT_EQUAL(EL_OK, el_settings_encode(&s, buf, sizeof buf, &n));
    el_settings_t back;
    TEST_ASSERT_EQUAL(EL_OK, el_settings_decode(buf, n, &back));
    TEST_ASSERT_EQUAL_STRING(s.upload_url, back.upload_url);
    TEST_ASSERT_EQUAL_STRING(s.wifi_password, back.wifi_password);
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_settings_encode(&s, buf, n - 1, &n));
}

void run_uplink_tests(void)
{
    RUN_TEST(test_url_rules);
    RUN_TEST(test_credentials);
    RUN_TEST(test_json_escape);
    RUN_TEST(test_body_one_result);
    RUN_TEST(test_body_nulls_and_test_message);
    RUN_TEST(test_body_batch_limits_and_rollback);
    RUN_TEST(test_classify);
    RUN_TEST(test_backoff_and_schedule);
    RUN_TEST(test_settings_wifi);
}
