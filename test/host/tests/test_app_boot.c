#include "unity.h"
#include "el_app_boot.h"

static void test_boot_mode_decision(void)
{
    el_app_boot_in_t in = { false, false, false, false };
    TEST_ASSERT_EQUAL(EL_APP_BOOT_NORMAL, el_app_boot_decide(&in));
    in.diag_flag = true;
    TEST_ASSERT_EQUAL(EL_APP_BOOT_DIAG, el_app_boot_decide(&in));
    in.diag_flag = false; in.bringup_build = true;
    TEST_ASSERT_EQUAL(EL_APP_BOOT_DIAG, el_app_boot_decide(&in));
    in.fixture_present = true;                       /* detection disabled: ignored */
    TEST_ASSERT_EQUAL(EL_APP_BOOT_DIAG, el_app_boot_decide(&in));
    in.fixture_detect_enabled = true;
    TEST_ASSERT_EQUAL(EL_APP_BOOT_FACTORY, el_app_boot_decide(&in));
    TEST_ASSERT_EQUAL(EL_APP_BOOT_NORMAL, el_app_boot_decide(NULL));
    TEST_ASSERT_EQUAL_STRING("DIAG", el_app_boot_mode_name(EL_APP_BOOT_DIAG));
    TEST_ASSERT_EQUAL_STRING("?", el_app_boot_mode_name(EL_APP_BOOT_MODE_COUNT));
}

static void test_flag_word_round_trip_and_rejects_garbage(void)
{
    el_app_boot_mode_t m = EL_APP_BOOT_NORMAL;
    TEST_ASSERT_TRUE(el_app_boot_flag_decode(el_app_boot_flag_encode(EL_APP_BOOT_DIAG), &m));
    TEST_ASSERT_EQUAL(EL_APP_BOOT_DIAG, m);
    TEST_ASSERT_TRUE(el_app_boot_flag_decode(el_app_boot_flag_encode(EL_APP_BOOT_FACTORY), &m));
    TEST_ASSERT_EQUAL(EL_APP_BOOT_FACTORY, m);
    TEST_ASSERT_FALSE(el_app_boot_flag_decode(el_app_boot_flag_encode(EL_APP_BOOT_NORMAL), &m));
    TEST_ASSERT_FALSE(el_app_boot_flag_decode(0x00000000u, &m));       /* cleared */
    TEST_ASSERT_FALSE(el_app_boot_flag_decode(0xFFFFFFFFu, &m));       /* random RAM */
    uint32_t w = el_app_boot_flag_encode(EL_APP_BOOT_DIAG);
    TEST_ASSERT_FALSE(el_app_boot_flag_decode(w ^ 0x00000100u, &m));    /* mode bit flipped */
    TEST_ASSERT_FALSE(el_app_boot_flag_decode((w & 0xFFFF0000u) | 0x0500u | 0xFAu, &m)); /* mode 5 */
}

static void test_tally_and_confirm_rule(void)
{
    el_app_tally_t t;
    el_app_tally_init(&t);
    el_app_tally_add(&t, "NVS", EL_TEST_PASS);
    el_app_tally_add(&t, "I2C_BUS", EL_TEST_WARN);   /* unexpected address: still confirms */
    el_app_tally_add(&t, "RTC_TICK", EL_TEST_FAIL);
    el_app_tally_add(&t, "TOUCH", EL_TEST_SKIP);
    el_app_tally_add(&t, "RTC", EL_TEST_FAIL);
    TEST_ASSERT_EQUAL_UINT16(2, t.fail);
    TEST_ASSERT_EQUAL_STRING("RTC_TICK", t.first_fail);
    TEST_ASSERT_TRUE(el_app_tally_may_confirm(&t));  /* a non-blocker failure must not roll back */
    el_app_tally_add(&t, "STORAGE", EL_TEST_FAIL);
    TEST_ASSERT_FALSE(el_app_tally_may_confirm(&t));
    TEST_ASSERT_EQUAL_UINT16(1, t.pass);
    TEST_ASSERT_EQUAL_UINT16(1, t.warn);
    TEST_ASSERT_EQUAL_UINT16(1, t.skip);
    TEST_ASSERT_TRUE(el_app_boot_is_blocker("I2C_BUS"));
    TEST_ASSERT_FALSE(el_app_boot_is_blocker("i2c_bus"));
    TEST_ASSERT_FALSE(el_app_boot_is_blocker(NULL));
    TEST_ASSERT_FALSE(el_app_tally_may_confirm(NULL));
}

void run_app_boot_tests(void)
{
    RUN_TEST(test_boot_mode_decision);
    RUN_TEST(test_flag_word_round_trip_and_rejects_garbage);
    RUN_TEST(test_tally_and_confirm_rule);
}
