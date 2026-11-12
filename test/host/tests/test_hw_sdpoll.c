#include "unity.h"
#include "el_hw_sdpoll.h"

static void test_no_card_backs_off(void)
{
    el_hw_sd_poll_t p;
    el_hw_sd_poll_init(&p);
    TEST_ASSERT_EQUAL(EL_HW_SD_ACT_MOUNT, el_hw_sd_poll_next(&p, 0));
    TEST_ASSERT_FALSE(el_hw_sd_poll_report(&p, EL_HW_SD_ACT_MOUNT, false, 0));
    TEST_ASSERT_EQUAL(EL_HW_SD_ACT_NONE, el_hw_sd_poll_next(&p, 2000));
    TEST_ASSERT_EQUAL(EL_HW_SD_ACT_NONE, el_hw_sd_poll_next(&p, EL_HW_SD_RETRY_MS - 1));
    TEST_ASSERT_EQUAL(EL_HW_SD_ACT_MOUNT, el_hw_sd_poll_next(&p, EL_HW_SD_RETRY_MS));
}

static void test_insert_then_remove(void)
{
    el_hw_sd_poll_t p;
    el_hw_sd_poll_init(&p);
    TEST_ASSERT_TRUE(el_hw_sd_poll_report(&p, EL_HW_SD_ACT_MOUNT, true, 100));   /* inserted */
    TEST_ASSERT_TRUE(p.mounted);
    TEST_ASSERT_EQUAL(EL_HW_SD_ACT_STATUS, el_hw_sd_poll_next(&p, 3000));
    TEST_ASSERT_FALSE(el_hw_sd_poll_report(&p, EL_HW_SD_ACT_STATUS, true, 3000));
    /* One failed CMD13 is a glitch, two in a row = pulled. */
    TEST_ASSERT_FALSE(el_hw_sd_poll_report(&p, EL_HW_SD_ACT_STATUS, false, 6000));
    TEST_ASSERT_FALSE(el_hw_sd_poll_report(&p, EL_HW_SD_ACT_STATUS, true, 9000));
    TEST_ASSERT_FALSE(el_hw_sd_poll_report(&p, EL_HW_SD_ACT_STATUS, false, 12000));
    TEST_ASSERT_TRUE(el_hw_sd_poll_report(&p, EL_HW_SD_ACT_STATUS, false, 15000));
    TEST_ASSERT_FALSE(p.mounted);
    /* Re-probe allowed immediately after a removal. */
    TEST_ASSERT_EQUAL(EL_HW_SD_ACT_MOUNT, el_hw_sd_poll_next(&p, 15001));
}

static void test_stale_reports_ignored(void)
{
    el_hw_sd_poll_t p;
    el_hw_sd_poll_init(&p);
    TEST_ASSERT_FALSE(el_hw_sd_poll_report(&p, EL_HW_SD_ACT_STATUS, false, 0));
    TEST_ASSERT_FALSE(el_hw_sd_poll_report(&p, EL_HW_SD_ACT_NONE, true, 0));
    el_hw_sd_poll_set_mounted(&p, true, 0);
    TEST_ASSERT_FALSE(el_hw_sd_poll_report(&p, EL_HW_SD_ACT_MOUNT, true, 0));
    el_hw_sd_poll_set_mounted(&p, false, 0);
    TEST_ASSERT_EQUAL(EL_HW_SD_ACT_MOUNT, el_hw_sd_poll_next(&p, 0));
    TEST_ASSERT_EQUAL(EL_HW_SD_ACT_NONE, el_hw_sd_poll_next(NULL, 0));
}

static void test_retry_survives_ms_wrap(void)
{
    el_hw_sd_poll_t p;
    el_hw_sd_poll_init(&p);
    uint32_t t = 0xFFFFF000u;                    /* 49.7 days of uptime, about to wrap */
    el_hw_sd_poll_report(&p, EL_HW_SD_ACT_MOUNT, false, t);
    TEST_ASSERT_EQUAL(EL_HW_SD_ACT_NONE, el_hw_sd_poll_next(&p, t + 100));
    TEST_ASSERT_EQUAL(EL_HW_SD_ACT_NONE, el_hw_sd_poll_next(&p, t + 4096 + 100));   /* wrapped */
    TEST_ASSERT_EQUAL(EL_HW_SD_ACT_MOUNT, el_hw_sd_poll_next(&p, t + EL_HW_SD_RETRY_MS));
}

void run_hw_sdpoll_tests(void)
{
    RUN_TEST(test_no_card_backs_off);
    RUN_TEST(test_insert_then_remove);
    RUN_TEST(test_stale_reports_ignored);
    RUN_TEST(test_retry_survives_ms_wrap);
}
