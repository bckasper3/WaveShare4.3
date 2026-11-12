#include "unity.h"
#include "el_ui_idle.h"

static void test_on_dim_off_and_wake(void)
{
    el_ui_idle_t t; bool ch;
    el_ui_idle_init(&t, 1000);
    TEST_ASSERT_EQUAL(EL_UI_PWR_ON, el_ui_idle_tick(&t, 1000 + 119999, 120, 600, false, &ch));
    TEST_ASSERT_FALSE(ch);
    TEST_ASSERT_EQUAL(EL_UI_PWR_DIM, el_ui_idle_tick(&t, 1000 + 120000, 120, 600, false, &ch));
    TEST_ASSERT_TRUE(ch);
    TEST_ASSERT_EQUAL(EL_UI_PWR_DIM, el_ui_idle_tick(&t, 1000 + 300000, 120, 600, false, &ch));
    TEST_ASSERT_FALSE(ch);
    TEST_ASSERT_EQUAL(EL_UI_PWR_OFF, el_ui_idle_tick(&t, 1000 + 600000, 120, 600, false, &ch));
    TEST_ASSERT_TRUE(ch);
    /* first touch on a dark screen only wakes it */
    TEST_ASSERT_TRUE(el_ui_idle_activity(&t, 700000));
    TEST_ASSERT_EQUAL(EL_UI_PWR_ON, t.state);
    TEST_ASSERT_FALSE(el_ui_idle_activity(&t, 700100));   /* next touch acts normally */
}

static void test_touch_while_dim_is_not_swallowed(void)
{
    el_ui_idle_t t;
    el_ui_idle_init(&t, 0);
    el_ui_idle_tick(&t, 200000, 120, 600, false, NULL);
    TEST_ASSERT_EQUAL(EL_UI_PWR_DIM, t.state);
    TEST_ASSERT_FALSE(el_ui_idle_activity(&t, 200001));
    TEST_ASSERT_EQUAL(EL_UI_PWR_ON, t.state);
}

static void test_keep_awake_during_measurement(void)
{
    el_ui_idle_t t; bool ch;
    el_ui_idle_init(&t, 0);
    for (uint32_t ms = 0; ms < 900000; ms += 1000)
        TEST_ASSERT_EQUAL(EL_UI_PWR_ON, el_ui_idle_tick(&t, ms, 120, 600, true, &ch));
    /* the idle timer restarts when the measurement ends */
    TEST_ASSERT_EQUAL(EL_UI_PWR_ON, el_ui_idle_tick(&t, 899000 + 119000, 120, 600, false, &ch));
    TEST_ASSERT_EQUAL(EL_UI_PWR_DIM, el_ui_idle_tick(&t, 899000 + 121000, 120, 600, false, &ch));
}

static void test_disabled_stages_and_wraparound(void)
{
    el_ui_idle_t t;
    el_ui_idle_init(&t, 0);
    TEST_ASSERT_EQUAL(EL_UI_PWR_ON, el_ui_idle_tick(&t, 0x7FFFFFFFu, 0, 0, false, NULL));  /* never */
    el_ui_idle_init(&t, 0);
    TEST_ASSERT_EQUAL(EL_UI_PWR_OFF, el_ui_idle_tick(&t, 600000, 0, 600, false, NULL));   /* off only */
    el_ui_idle_init(&t, 0xFFFFF000u);                                                     /* near wrap */
    TEST_ASSERT_EQUAL(EL_UI_PWR_ON, el_ui_idle_tick(&t, 0x00001000u, 120, 600, false, NULL));
    TEST_ASSERT_EQUAL(EL_UI_PWR_DIM, el_ui_idle_tick(&t, 0xFFFFF000u + 130000u, 120, 600, false, NULL));
    TEST_ASSERT_EQUAL(EL_UI_PWR_ON, el_ui_idle_tick(NULL, 0, 1, 1, false, NULL));
    TEST_ASSERT_FALSE(el_ui_idle_activity(NULL, 0));
}

static void test_backlight_levels(void)
{
    TEST_ASSERT_EQUAL_UINT8(80, el_ui_idle_backlight_pct(EL_UI_PWR_ON, 80));
    TEST_ASSERT_EQUAL_UINT8(10, el_ui_idle_backlight_pct(EL_UI_PWR_ON, 0));     /* never black while on */
    TEST_ASSERT_EQUAL_UINT8(100, el_ui_idle_backlight_pct(EL_UI_PWR_ON, 250));
    TEST_ASSERT_EQUAL_UINT8(EL_UI_DIM_PCT, el_ui_idle_backlight_pct(EL_UI_PWR_DIM, 80));
    TEST_ASSERT_EQUAL_UINT8(10, el_ui_idle_backlight_pct(EL_UI_PWR_DIM, 10));
    TEST_ASSERT_EQUAL_UINT8(0, el_ui_idle_backlight_pct(EL_UI_PWR_OFF, 80));
}

void run_ui_idle_tests(void)
{
    RUN_TEST(test_on_dim_off_and_wake);
    RUN_TEST(test_touch_while_dim_is_not_swallowed);
    RUN_TEST(test_keep_awake_during_measurement);
    RUN_TEST(test_disabled_stages_and_wraparound);
    RUN_TEST(test_backlight_levels);
}
