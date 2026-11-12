#include "unity.h"
#include "el_app_console.h"

static void test_parse_u32(void)
{
    uint32_t v = 0;
    TEST_ASSERT_TRUE(el_app_parse_u32("1000", 100, 60000, &v)); TEST_ASSERT_EQUAL_UINT32(1000, v);
    TEST_ASSERT_TRUE(el_app_parse_u32("0x40", 0, 0x7F, &v));    TEST_ASSERT_EQUAL_UINT32(0x40, v);
    TEST_ASSERT_TRUE(el_app_parse_u32("4294967295", 0, UINT32_MAX, &v));
    TEST_ASSERT_FALSE(el_app_parse_u32("4294967296", 0, UINT32_MAX, &v));
    TEST_ASSERT_FALSE(el_app_parse_u32("99", 100, 60000, &v));
    TEST_ASSERT_FALSE(el_app_parse_u32("12a", 0, 100, &v));
    TEST_ASSERT_FALSE(el_app_parse_u32("-1", 0, 100, &v));
    TEST_ASSERT_FALSE(el_app_parse_u32("0x", 0, 100, &v));
    TEST_ASSERT_FALSE(el_app_parse_u32("", 0, 100, &v));
    TEST_ASSERT_FALSE(el_app_parse_u32(NULL, 0, 100, &v));
}

static void test_parse_hex_byte(void)
{
    uint8_t b = 0;
    TEST_ASSERT_TRUE(el_app_parse_hex_u8("40", &b));   TEST_ASSERT_EQUAL_HEX8(0x40, b);
    TEST_ASSERT_TRUE(el_app_parse_hex_u8("0x5d", &b)); TEST_ASSERT_EQUAL_HEX8(0x5D, b);
    TEST_ASSERT_TRUE(el_app_parse_hex_u8("F", &b));    TEST_ASSERT_EQUAL_HEX8(0x0F, b);
    TEST_ASSERT_FALSE(el_app_parse_hex_u8("100", &b));
    TEST_ASSERT_FALSE(el_app_parse_hex_u8("0x", &b));
    TEST_ASSERT_FALSE(el_app_parse_hex_u8("G1", &b));
    TEST_ASSERT_FALSE(el_app_parse_hex_u8("", &b));
    TEST_ASSERT_FALSE(el_app_parse_hex_u8(NULL, &b));
}

static void test_parse_onoff_float_word(void)
{
    bool v = false;
    TEST_ASSERT_TRUE(el_app_parse_onoff("on", &v));   TEST_ASSERT_TRUE(v);
    TEST_ASSERT_TRUE(el_app_parse_onoff("0", &v));    TEST_ASSERT_FALSE(v);
    TEST_ASSERT_TRUE(el_app_parse_onoff("True", &v)); TEST_ASSERT_TRUE(v);
    TEST_ASSERT_FALSE(el_app_parse_onoff("2", &v));
    TEST_ASSERT_FALSE(el_app_parse_onoff(NULL, &v));
    float f = 0;
    TEST_ASSERT_TRUE(el_app_parse_float("123.5", &f)); TEST_ASSERT_EQUAL_FLOAT(123.5f, f);
    TEST_ASSERT_TRUE(el_app_parse_float("-2e3", &f));  TEST_ASSERT_EQUAL_FLOAT(-2000.0f, f);
    TEST_ASSERT_FALSE(el_app_parse_float("12degC", &f));
    TEST_ASSERT_FALSE(el_app_parse_float("nan", &f));
    TEST_ASSERT_FALSE(el_app_parse_float("1e40", &f));
    TEST_ASSERT_FALSE(el_app_parse_float("", &f));
    TEST_ASSERT_TRUE(el_app_word_eq("Boot", "BOOT"));
    TEST_ASSERT_FALSE(el_app_word_eq("BOOTX", "BOOT"));
    TEST_ASSERT_FALSE(el_app_word_eq(NULL, "BOOT"));
}

static void test_engineer_verdict_keeps_lockout(void)
{
    el_session_t s;
    el_session_init(&s, 300000u, 0);
    uint32_t retry = 0;
    TEST_ASSERT_EQUAL(EL_LOGIN_DISABLED, el_app_login_verdict(&s, EL_LEVEL_ENGINEER, false, true, 10, &retry));
    TEST_ASSERT_EQUAL(EL_LEVEL_OPERATOR, el_session_level(&s, 10));
    for (int i = 0; i < EL_LOGIN_FREE_TRIES; i++)
        TEST_ASSERT_EQUAL(EL_LOGIN_WRONG, el_app_login_verdict(&s, EL_LEVEL_ENGINEER, true, false, 100, &retry));
    TEST_ASSERT_EQUAL_UINT32(EL_LOGIN_LOCK_MS, retry);
    /* even the right code is refused while locked */
    TEST_ASSERT_EQUAL(EL_LOGIN_LOCKED, el_app_login_verdict(&s, EL_LEVEL_ENGINEER, true, true, 1000, &retry));
    TEST_ASSERT_EQUAL_UINT32(EL_LOGIN_LOCK_MS - 900u, retry);
    TEST_ASSERT_EQUAL(EL_LOGIN_OK, el_app_login_verdict(&s, EL_LEVEL_ENGINEER, true, true, 100 + EL_LOGIN_LOCK_MS, &retry));
    TEST_ASSERT_EQUAL(EL_LEVEL_ENGINEER, el_session_level(&s, 200 + EL_LOGIN_LOCK_MS));
    TEST_ASSERT_EQUAL(EL_LOGIN_BAD_LEVEL, el_app_login_verdict(&s, EL_LEVEL_OPERATOR, true, true, 0, NULL));
    TEST_ASSERT_EQUAL(EL_LOGIN_BAD_LEVEL, el_app_login_verdict(NULL, EL_LEVEL_ENGINEER, true, true, 0, NULL));
}

void run_app_console_tests(void)
{
    RUN_TEST(test_parse_u32);
    RUN_TEST(test_parse_hex_byte);
    RUN_TEST(test_parse_onoff_float_word);
    RUN_TEST(test_engineer_verdict_keeps_lockout);
}
