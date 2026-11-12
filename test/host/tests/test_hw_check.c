#include <string.h>
#include "unity.h"
#include "el_hw_check.h"
#include "el_hw_pattern.h"

static void test_mem_patterns_detect_faults(void)
{
    static uint32_t buf[4096];
    for (unsigned p = 0; p < EL_HW_MEM_PASSES; p++) {
        size_t first = 0;
        el_hw_mem_fill(buf, 4096, p, 0x1234u);
        TEST_ASSERT_EQUAL_size_t(0, el_hw_mem_check(buf, 4096, p, 0x1234u, &first));
        TEST_ASSERT_EQUAL_size_t(4096, first);
        buf[1000] ^= 0x00010000u;                       /* one flipped bit */
        TEST_ASSERT_EQUAL_size_t(1, el_hw_mem_check(buf, 4096, p, 0x1234u, &first));
        TEST_ASSERT_EQUAL_size_t(1000, first);
    }
    /* Address aliasing (upper half mirrors lower half) is caught by pass 0. */
    el_hw_mem_fill(buf, 4096, 0, 7);
    memcpy(&buf[2048], &buf[0], 2048 * sizeof buf[0]);
    TEST_ASSERT_EQUAL_size_t(2048, el_hw_mem_check(buf, 4096, 0, 7, NULL));
    /* Passes 0 and 1 together put both levels in every bit of every word. */
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, el_hw_mem_word(5, 0, 9) ^ el_hw_mem_word(5, 1, 9));
    TEST_ASSERT_EQUAL_HEX32(1u << 5, el_hw_mem_word(37, 2, 0));
    TEST_ASSERT_EQUAL_size_t(10, el_hw_mem_check(NULL, 10, 0, 0, NULL));
}

static void test_rtc_tick(void)
{
    TEST_ASSERT_EQUAL(EL_TEST_PASS, el_hw_judge_rtc_tick(1000, 1001));
    TEST_ASSERT_EQUAL(EL_TEST_PASS, el_hw_judge_rtc_tick(1000, 1002));
    TEST_ASSERT_EQUAL(EL_TEST_FAIL, el_hw_judge_rtc_tick(1000, 1000));   /* stopped */
    TEST_ASSERT_EQUAL(EL_TEST_FAIL, el_hw_judge_rtc_tick(1000, 1003));
    TEST_ASSERT_EQUAL(EL_TEST_FAIL, el_hw_judge_rtc_tick(1000, 999));
}

static void test_dio_and_sizes(void)
{
    TEST_ASSERT_TRUE(el_hw_dio_expected_di0(false));   /* DO1 off -> 12 V via 2.2 k -> DI0 active */
    TEST_ASSERT_FALSE(el_hw_dio_expected_di0(true));
    TEST_ASSERT_EQUAL(EL_TEST_PASS, el_hw_judge_flash_size(16u * 1024u * 1024u));
    TEST_ASSERT_EQUAL(EL_TEST_FAIL, el_hw_judge_flash_size(8u * 1024u * 1024u));
    TEST_ASSERT_EQUAL(EL_TEST_FAIL, el_hw_judge_flash_size(32u * 1024u * 1024u));
    TEST_ASSERT_EQUAL(EL_TEST_PASS, el_hw_judge_psram_size(8u * 1024u * 1024u));
    TEST_ASSERT_EQUAL(EL_TEST_FAIL, el_hw_judge_psram_size(2u * 1024u * 1024u));
    TEST_ASSERT_EQUAL(EL_TEST_FAIL, el_hw_judge_psram_size(0));
    TEST_ASSERT_EQUAL(EL_TEST_WARN, el_hw_judge_psram_size(16u * 1024u * 1024u));
}

static void test_gt911_id(void)
{
    const uint8_t ok[4] = { '9', '1', '1', 0 };
    const uint8_t other[4] = { '9', '1', '4', '7' };
    const uint8_t junk[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    TEST_ASSERT_EQUAL(EL_TEST_PASS, el_hw_judge_gt911_id(ok));
    TEST_ASSERT_EQUAL(EL_TEST_WARN, el_hw_judge_gt911_id(other));
    TEST_ASSERT_EQUAL(EL_TEST_FAIL, el_hw_judge_gt911_id(junk));
    TEST_ASSERT_EQUAL(EL_TEST_FAIL, el_hw_judge_gt911_id(NULL));
}

static void test_display_pattern(void)
{
    const int w = 800, h = 480;
    const uint16_t red = EL_RGB565(255, 0, 0);
    /* Border: every edge pixel red. */
    for (int x = 0; x < w; x++) {
        TEST_ASSERT_EQUAL_HEX16(red, el_hw_pattern_rgb565(x, 0, w, h));
        TEST_ASSERT_EQUAL_HEX16(red, el_hw_pattern_rgb565(x, h - 1, w, h));
    }
    for (int y = 0; y < h; y++) {
        TEST_ASSERT_EQUAL_HEX16(red, el_hw_pattern_rgb565(0, y, w, h));
        TEST_ASSERT_EQUAL_HEX16(red, el_hw_pattern_rgb565(w - 1, y, w, h));
    }
    /* Bars: white first, black last; grid lines grey. */
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, el_hw_pattern_rgb565(10, 10, w, h));
    TEST_ASSERT_EQUAL_HEX16(0x0000, el_hw_pattern_rgb565(790, 10, w, h));
    TEST_ASSERT_EQUAL_HEX16(EL_RGB565(255, 255, 0), el_hw_pattern_rgb565(110, 10, w, h));
    TEST_ASSERT_EQUAL_HEX16(EL_RGB565(0, 0, 255), el_hw_pattern_rgb565(610, 10, w, h));
    TEST_ASSERT_EQUAL_HEX16(EL_RGB565(128, 128, 128), el_hw_pattern_rgb565(40, 10, w, h));
    TEST_ASSERT_EQUAL_HEX16(EL_RGB565(128, 128, 128), el_hw_pattern_rgb565(10, 40, w, h));
    /* Ramps: green strip steps through all 64 codes, monotonic left to right. */
    int gy = 320 + 40 + 20;
    unsigned prev = 0, distinct = 0;
    for (int x = 1; x < w - 1; x++) {
        uint16_t c = el_hw_pattern_rgb565(x, gy, w, h);
        TEST_ASSERT_EQUAL_HEX16(0, c & 0xF81F);                /* green only */
        unsigned g = (unsigned)(c >> 5) & 0x3Fu;
        TEST_ASSERT_TRUE(g >= prev);
        if (g != prev || x == 1) distinct++;
        prev = g;
    }
    TEST_ASSERT_EQUAL_UINT(63, prev);
    TEST_ASSERT_EQUAL_UINT(64, distinct);
    /* Red, blue and grey strips. */
    TEST_ASSERT_EQUAL_HEX16(0xF800, el_hw_pattern_rgb565(798, 330, w, h));
    TEST_ASSERT_EQUAL_HEX16(0x001F, el_hw_pattern_rgb565(798, 420, w, h));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, el_hw_pattern_rgb565(798, 460, w, h));
    TEST_ASSERT_EQUAL_HEX16(0x0000, el_hw_pattern_rgb565(1, 460, w, h));
    /* Out of range. */
    TEST_ASSERT_EQUAL_HEX16(0, el_hw_pattern_rgb565(-1, 5, w, h));
    TEST_ASSERT_EQUAL_HEX16(0, el_hw_pattern_rgb565(5, 480, w, h));
    TEST_ASSERT_EQUAL_HEX16(0, el_hw_pattern_rgb565(0, 0, 0, 0));
}

void run_hw_check_tests(void)
{
    RUN_TEST(test_mem_patterns_detect_faults);
    RUN_TEST(test_rtc_tick);
    RUN_TEST(test_dio_and_sizes);
    RUN_TEST(test_gt911_id);
    RUN_TEST(test_display_pattern);
}
