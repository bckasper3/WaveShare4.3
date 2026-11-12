#include "unity.h"
#include "el_crc.h"

static void test_crc_check_value(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, el_crc32("123456789", 9));
}

static void test_crc_empty_is_zero(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000000, el_crc32("", 0));
}

static void test_crc_incremental_matches_oneshot(void)
{
    const char *s = "EnvLogger T + RH 1.0";
    uint32_t c = el_crc32_update(0, s, 7);
    c = el_crc32_update(c, s + 7, 13);
    TEST_ASSERT_EQUAL_HEX32(el_crc32(s, 20), c);
}

void run_crc_tests(void)
{
    RUN_TEST(test_crc_check_value);
    RUN_TEST(test_crc_empty_is_zero);
    RUN_TEST(test_crc_incremental_matches_oneshot);
}
