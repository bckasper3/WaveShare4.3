#include "unity.h"
#include "el_app_dio.h"
#include "el_settings.h"

static void test_poll_period_trade_off(void)
{
    TEST_ASSERT_EQUAL_UINT32(50, el_app_dio_period_ms(true, false));
    TEST_ASSERT_EQUAL_UINT32(50, el_app_dio_period_ms(true, true));
    TEST_ASSERT_EQUAL_UINT32(50, el_app_dio_period_ms(false, true));
    TEST_ASSERT_EQUAL_UINT32(1000, el_app_dio_period_ms(false, false));
}

static void test_first_update_adopts_state_without_edge(void)
{
    el_app_di_t d; el_app_di_ev_t ev;
    el_app_di_init(&d);
    el_app_di_update(&d, true, false, &ev);       /* foot switch held at power-up */
    TEST_ASSERT_TRUE(ev.changed);
    TEST_ASSERT_FALSE(ev.rise[0]);
    TEST_ASSERT_EQUAL_UINT8(1, ev.bits);
    el_app_di_update(&d, true, false, &ev);
    TEST_ASSERT_FALSE(ev.changed);
}

static void test_rising_edge_needs_two_reads_and_ignores_a_glitch(void)
{
    el_app_di_t d; el_app_di_ev_t ev;
    el_app_di_init(&d);
    el_app_di_update(&d, false, false, &ev);
    el_app_di_update(&d, true, false, &ev);       /* single 50 ms glitch */
    TEST_ASSERT_FALSE(ev.changed);
    el_app_di_update(&d, false, false, &ev);
    TEST_ASSERT_FALSE(ev.changed);
    el_app_di_update(&d, true, false, &ev);       /* real press */
    TEST_ASSERT_FALSE(ev.rise[0]);
    el_app_di_update(&d, true, false, &ev);
    TEST_ASSERT_TRUE(ev.changed);
    TEST_ASSERT_TRUE(ev.rise[0]);
    TEST_ASSERT_FALSE(ev.fall[0]);
    TEST_ASSERT_EQUAL_UINT8(1, ev.bits);
    el_app_di_update(&d, true, false, &ev);       /* held: no repeat */
    TEST_ASSERT_FALSE(ev.rise[0]);
    el_app_di_update(&d, false, false, &ev);
    el_app_di_update(&d, false, false, &ev);
    TEST_ASSERT_TRUE(ev.fall[0]);
    TEST_ASSERT_EQUAL_UINT8(0, ev.bits);
}

static void test_inputs_are_independent(void)
{
    el_app_di_t d; el_app_di_ev_t ev;
    el_app_di_init(&d);
    el_app_di_update(&d, false, false, &ev);
    el_app_di_update(&d, false, true, &ev);
    el_app_di_update(&d, true, true, &ev);
    TEST_ASSERT_TRUE(ev.rise[1]);
    TEST_ASSERT_FALSE(ev.rise[0]);
    TEST_ASSERT_EQUAL_UINT8(2, ev.bits);
    el_app_di_update(&d, true, true, &ev);
    TEST_ASSERT_TRUE(ev.rise[0]);
    TEST_ASSERT_EQUAL_UINT8(3, ev.bits);
    el_app_di_update(NULL, true, true, &ev);      /* must not crash */
}

static void test_do1_alarm_modes(void)
{
    TEST_ASSERT_TRUE(el_app_do1_matches(EL_DO1_ON_ALARM, EL_RES_ALARM));
    TEST_ASSERT_FALSE(el_app_do1_matches(EL_DO1_ON_ALARM, EL_RES_NORMAL));
    TEST_ASSERT_FALSE(el_app_do1_matches(EL_DO1_ON_ALARM, EL_RES_ERROR));
    TEST_ASSERT_FALSE(el_app_do1_matches(EL_DO1_OFF, EL_RES_ALARM));
    TEST_ASSERT_FALSE(el_app_do1_matches(EL_DO1_BUSY, EL_RES_ALARM));
    TEST_ASSERT_FALSE(el_app_do1_matches(99, EL_RES_ALARM));
}

static void test_do1_follows_last_result_and_clear(void)
{
    el_app_do1_t o;
    el_app_do1_init(&o, EL_DO1_ON_ALARM);
    TEST_ASSERT_FALSE(el_app_do1_output(&o, false));
    el_app_do1_result(&o, EL_RES_ALARM);              /* cold room at 9 degC, high limit 8 */
    TEST_ASSERT_TRUE(el_app_do1_output(&o, false));
    TEST_ASSERT_TRUE(el_app_do1_output(&o, true));    /* stays on while the next one runs */
    el_app_do1_clear(&o);                             /* PLC acknowledges via reg 100 = 3 */
    TEST_ASSERT_FALSE(el_app_do1_output(&o, false));
    el_app_do1_set_mode(&o, EL_DO1_ON_ALARM);         /* cleared stays cleared */
    TEST_ASSERT_FALSE(el_app_do1_output(&o, false));
    el_app_do1_result(&o, EL_RES_ALARM);              /* next reading still too warm */
    TEST_ASSERT_TRUE(el_app_do1_output(&o, false));
    el_app_do1_set_mode(&o, EL_DO1_OFF);              /* mode change re-evaluates */
    TEST_ASSERT_FALSE(el_app_do1_output(&o, false));
    el_app_do1_set_mode(&o, EL_DO1_ON_ALARM);
    TEST_ASSERT_TRUE(el_app_do1_output(&o, false));
    el_app_do1_result(&o, EL_RES_NORMAL);             /* back inside the limits */
    TEST_ASSERT_FALSE(el_app_do1_output(&o, false));
    el_app_do1_result(&o, EL_RES_ERROR);              /* a failed reading never alarms */
    TEST_ASSERT_FALSE(el_app_do1_output(&o, false));
    el_app_do1_set_mode(&o, EL_DO1_OFF);
    el_app_do1_result(&o, EL_RES_ALARM);
    TEST_ASSERT_FALSE(el_app_do1_output(&o, false));
}

static void test_do1_busy_mode(void)
{
    el_app_do1_t o;
    el_app_do1_init(&o, EL_DO1_BUSY);
    TEST_ASSERT_FALSE(el_app_do1_output(&o, false));
    TEST_ASSERT_TRUE(el_app_do1_output(&o, true));
    el_app_do1_result(&o, EL_RES_ALARM);
    TEST_ASSERT_FALSE(el_app_do1_output(&o, false));
    TEST_ASSERT_FALSE(el_app_do1_output(NULL, true));
}

void run_app_dio_tests(void)
{
    RUN_TEST(test_poll_period_trade_off);
    RUN_TEST(test_first_update_adopts_state_without_edge);
    RUN_TEST(test_rising_edge_needs_two_reads_and_ignores_a_glitch);
    RUN_TEST(test_inputs_are_independent);
    RUN_TEST(test_do1_alarm_modes);
    RUN_TEST(test_do1_follows_last_result_and_clear);
    RUN_TEST(test_do1_busy_mode);
}
