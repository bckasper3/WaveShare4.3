#include <stdio.h>
#include "el_hw_check.h"

uint32_t el_hw_mem_word(size_t i, unsigned pass, uint32_t seed)
{
    uint32_t a = (uint32_t)i * 0x9E3779B1u + seed;   /* odd multiplier: distinct per index */
    switch (pass) {
    case 0:  return a;
    case 1:  return ~a;
    default: return 1u << (i % 32u);
    }
}

void el_hw_mem_fill(uint32_t *buf, size_t words, unsigned pass, uint32_t seed)
{
    if (!buf) return;
    for (size_t i = 0; i < words; i++) buf[i] = el_hw_mem_word(i, pass, seed);
}

size_t el_hw_mem_check(const uint32_t *buf, size_t words, unsigned pass, uint32_t seed, size_t *first_bad)
{
    size_t bad = 0, first = words;
    if (buf) {
        for (size_t i = 0; i < words; i++) {
            if (buf[i] != el_hw_mem_word(i, pass, seed)) {
                if (!bad) first = i;
                bad++;
            }
        }
    }
    if (first_bad) *first_bad = first;
    return buf ? bad : words;
}

el_test_outcome_t el_hw_judge_rtc_tick(int64_t t0_s, int64_t t1_s)
{
    int64_t d = t1_s - t0_s;
    /* 1100 ms always crosses at least one second boundary and at most two. */
    return (d == 1 || d == 2) ? EL_TEST_PASS : EL_TEST_FAIL;
}

bool el_hw_dio_expected_di0(bool do1_on) { return !do1_on; }

el_test_outcome_t el_hw_judge_flash_size(uint32_t bytes)
{
    return bytes == EL_HW_FLASH_EXPECT_BYTES ? EL_TEST_PASS : EL_TEST_FAIL;
}

el_test_outcome_t el_hw_judge_psram_size(size_t bytes)
{
    /* esp_psram_get_size() reports the chip size; a smaller value = wrong module or a failed
     * octal-PSRAM bring-up. Larger is unexpected but harmless. */
    if (bytes < EL_HW_PSRAM_EXPECT_BYTES) return EL_TEST_FAIL;
    return bytes == EL_HW_PSRAM_EXPECT_BYTES ? EL_TEST_PASS : EL_TEST_WARN;
}

el_test_outcome_t el_hw_judge_gt911_id(const uint8_t id[4])
{
    if (!id) return EL_TEST_FAIL;
    if (id[0] == '9' && id[1] == '1' && id[2] == '1') return EL_TEST_PASS;
    /* Another Goodix part answering (GT9147, GT928 ...) is still a touch controller. */
    bool digits = id[0] >= '0' && id[0] <= '9' && id[1] >= '0' && id[1] <= '9';
    return digits ? EL_TEST_WARN : EL_TEST_FAIL;
}
