#include "unity.h"
#include "el_access.h"

static void test_every_action_has_a_level_and_levels_are_cumulative(void)
{
    for (int a = 0; a < EL_ACT_COUNT; a++) {
        el_level_t min = el_action_min_level((el_action_t)a);
        TEST_ASSERT_TRUE(min < EL_LEVEL_COUNT);
        for (int l = 0; l < EL_LEVEL_COUNT; l++)
            TEST_ASSERT_EQUAL(l >= (int)min, el_access_allows((el_level_t)l, (el_action_t)a));
    }
}

static void test_key_permissions(void)
{
    TEST_ASSERT_TRUE (el_access_allows(EL_LEVEL_OPERATOR, EL_ACT_MEASURE));
    TEST_ASSERT_TRUE (el_access_allows(EL_LEVEL_OPERATOR, EL_ACT_VIEW_DIAGNOSTICS));
    TEST_ASSERT_FALSE(el_access_allows(EL_LEVEL_OPERATOR, EL_ACT_CALIBRATE));
    TEST_ASSERT_TRUE (el_access_allows(EL_LEVEL_ADMIN,    EL_ACT_CALIBRATE));
    TEST_ASSERT_FALSE(el_access_allows(EL_LEVEL_ADMIN,    EL_ACT_RAW_IO));
    TEST_ASSERT_FALSE(el_access_allows(EL_LEVEL_ADMIN,    EL_ACT_SIMULATION));
    TEST_ASSERT_TRUE (el_access_allows(EL_LEVEL_ENGINEER, EL_ACT_RAW_IO));
    /* unknown action or level: deny */
    TEST_ASSERT_FALSE(el_access_allows(EL_LEVEL_ADMIN, (el_action_t)999));
    TEST_ASSERT_FALSE(el_access_allows((el_level_t)7, EL_ACT_MEASURE));
}

static void test_login_ok_and_wrong(void)
{
    el_session_t s; el_session_init(&s, 0, 0);
    TEST_ASSERT_EQUAL(EL_LEVEL_OPERATOR, el_session_level(&s, 0));
    TEST_ASSERT_EQUAL(EL_LOGIN_WRONG, el_session_login(&s, EL_LEVEL_ADMIN, "1235", "1234", 10, NULL));
    TEST_ASSERT_EQUAL(EL_LOGIN_WRONG, el_session_login(&s, EL_LEVEL_ADMIN, "12345", "1234", 10, NULL));
    TEST_ASSERT_EQUAL(EL_LOGIN_WRONG, el_session_login(&s, EL_LEVEL_ADMIN, "", "1234", 10, NULL));
    TEST_ASSERT_EQUAL(EL_LEVEL_OPERATOR, el_session_level(&s, 10));
    TEST_ASSERT_EQUAL(EL_LOGIN_OK, el_session_login(&s, EL_LEVEL_ADMIN, "1234", "1234", 20, NULL));
    TEST_ASSERT_EQUAL(EL_LEVEL_ADMIN, el_session_level(&s, 20));
    TEST_ASSERT_EQUAL(0, s.failures);                       /* success clears the count */
    el_session_logout(&s);
    TEST_ASSERT_EQUAL(EL_LEVEL_OPERATOR, el_session_level(&s, 30));
}

static void test_no_admin_pin_is_open_but_no_engineer_code_is_disabled(void)
{
    el_session_t s; el_session_init(&s, 0, 0);
    TEST_ASSERT_EQUAL(EL_LOGIN_OK, el_session_login(&s, EL_LEVEL_ADMIN, "anything", "", 0, NULL));
    TEST_ASSERT_EQUAL(EL_LOGIN_OK, el_session_login(&s, EL_LEVEL_ADMIN, NULL, NULL, 0, NULL));
    el_session_logout(&s);
    TEST_ASSERT_EQUAL(EL_LOGIN_DISABLED, el_session_login(&s, EL_LEVEL_ENGINEER, "", "", 0, NULL));
    TEST_ASSERT_EQUAL(EL_LOGIN_DISABLED, el_session_login(&s, EL_LEVEL_ENGINEER, "x", NULL, 0, NULL));
    TEST_ASSERT_EQUAL(EL_LOGIN_BAD_LEVEL, el_session_login(&s, EL_LEVEL_OPERATOR, "x", "x", 0, NULL));
    TEST_ASSERT_EQUAL(EL_LEVEL_OPERATOR, el_session_level(&s, 0));
}

static void test_lockout_then_doubling(void)
{
    el_session_t s; el_session_init(&s, 0, 0);
    uint32_t retry = 0, t = 1000;
    for (int i = 0; i < EL_LOGIN_FREE_TRIES - 1; i++)
        TEST_ASSERT_EQUAL(EL_LOGIN_WRONG, el_session_login(&s, EL_LEVEL_ADMIN, "0", "1234", t, &retry));
    TEST_ASSERT_EQUAL(EL_LOGIN_WRONG, el_session_login(&s, EL_LEVEL_ADMIN, "0", "1234", t, &retry));
    TEST_ASSERT_EQUAL_UINT32(EL_LOGIN_LOCK_MS, retry);     /* 5th failure starts the lock */

    /* even the right code is refused while locked */
    TEST_ASSERT_EQUAL(EL_LOGIN_LOCKED, el_session_login(&s, EL_LEVEL_ADMIN, "1234", "1234", t + 1000, &retry));
    TEST_ASSERT_EQUAL_UINT32(EL_LOGIN_LOCK_MS - 1000, retry);

    t += EL_LOGIN_LOCK_MS;                                  /* lock expired */
    TEST_ASSERT_EQUAL(EL_LOGIN_WRONG, el_session_login(&s, EL_LEVEL_ADMIN, "0", "1234", t, &retry));
    TEST_ASSERT_EQUAL_UINT32(2 * EL_LOGIN_LOCK_MS, retry);  /* doubled */

    t += 2 * EL_LOGIN_LOCK_MS;
    TEST_ASSERT_EQUAL(EL_LOGIN_OK, el_session_login(&s, EL_LEVEL_ADMIN, "1234", "1234", t, &retry));
}

static void test_lockout_is_capped(void)
{
    el_session_t s; el_session_init(&s, 0, 0);
    uint32_t retry = 0, t = 0;
    for (int i = 0; i < 40; i++) {
        el_session_login(&s, EL_LEVEL_ADMIN, "0", "1234", t, &retry);
        t += EL_LOGIN_LOCK_MAX_MS;                          /* wait out every lock */
    }
    TEST_ASSERT_EQUAL_UINT32(EL_LOGIN_LOCK_MAX_MS, retry);
}

static void test_idle_timeout_and_touch(void)
{
    el_session_t s; el_session_init(&s, 300000, 0);
    el_session_login(&s, EL_LEVEL_ENGINEER, "246810", "246810", 1000, NULL);
    TEST_ASSERT_EQUAL(EL_LEVEL_ENGINEER, el_session_level(&s, 300999));
    el_session_touch(&s, 300999);
    TEST_ASSERT_EQUAL(EL_LEVEL_ENGINEER, el_session_level(&s, 600998));
    TEST_ASSERT_EQUAL(EL_LEVEL_OPERATOR, el_session_level(&s, 600999));
}

static void test_timers_survive_ms_counter_wrap(void)
{
    el_session_t s; el_session_init(&s, 60000, 0);
    uint32_t t = 0xFFFFF000u;                               /* 4 s before the 32-bit wrap */
    el_session_login(&s, EL_LEVEL_ADMIN, "1", "1", t, NULL);
    TEST_ASSERT_EQUAL(EL_LEVEL_ADMIN, el_session_level(&s, t + 30000));   /* wrapped, still in time */
    TEST_ASSERT_EQUAL(EL_LEVEL_OPERATOR, el_session_level(&s, t + 60000));
}

void run_access_tests(void)
{
    RUN_TEST(test_every_action_has_a_level_and_levels_are_cumulative);
    RUN_TEST(test_key_permissions);
    RUN_TEST(test_login_ok_and_wrong);
    RUN_TEST(test_no_admin_pin_is_open_but_no_engineer_code_is_disabled);
    RUN_TEST(test_lockout_then_doubling);
    RUN_TEST(test_lockout_is_capped);
    RUN_TEST(test_idle_timeout_and_touch);
    RUN_TEST(test_timers_survive_ms_counter_wrap);
}
