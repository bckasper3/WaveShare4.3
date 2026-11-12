#include "unity.h"
#include "el_hw_pcf85063.h"

static void test_bcd(void)
{
    uint8_t v = 0;
    TEST_ASSERT_EQUAL(EL_OK, el_hw_bcd_to_bin(0x59, &v)); TEST_ASSERT_EQUAL_UINT8(59, v);
    TEST_ASSERT_EQUAL(EL_OK, el_hw_bcd_to_bin(0x00, &v)); TEST_ASSERT_EQUAL_UINT8(0, v);
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_hw_bcd_to_bin(0x5A, &v));
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_hw_bcd_to_bin(0xA0, &v));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_hw_bcd_to_bin(0x12, NULL));
    TEST_ASSERT_EQUAL_HEX8(0x99, el_hw_bin_to_bcd(99));
    TEST_ASSERT_EQUAL_HEX8(0x07, el_hw_bin_to_bcd(7));
    TEST_ASSERT_EQUAL_HEX8(0x99, el_hw_bin_to_bcd(150));       /* clamped */
    for (uint8_t i = 0; i < 100; i++) {
        uint8_t back;
        TEST_ASSERT_EQUAL(EL_OK, el_hw_bcd_to_bin(el_hw_bin_to_bcd(i), &back));
        TEST_ASSERT_EQUAL_UINT8(i, back);
    }
}

static void test_weekday(void)
{
    TEST_ASSERT_EQUAL_UINT8(4, el_hw_weekday(0));               /* 1970-01-01 Thursday */
    TEST_ASSERT_EQUAL_UINT8(6, el_hw_weekday(946684800));       /* 2000-01-01 Saturday */
    TEST_ASSERT_EQUAL_UINT8(5, el_hw_weekday(1793318400));      /* 2026-10-30 Friday */
    TEST_ASSERT_EQUAL_UINT8(0, el_hw_weekday(1793318400 + 2 * 86400 + 86399)); /* Sunday 23:59:59 */
    TEST_ASSERT_EQUAL_UINT8(3, el_hw_weekday(-1));              /* 1969-12-31 Wednesday */
}

static void test_encode_known_date(void)
{
    uint8_t r[7];
    /* 2026-10-30T14:05:09Z, Friday */
    TEST_ASSERT_EQUAL(EL_OK, el_hw_pcf85063_encode(1793369109, r));
    const uint8_t want[7] = { 0x09, 0x05, 0x14, 0x30, 5, 0x10, 0x26 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, r, 7);
}

static void test_decode_known_date_and_os_flag(void)
{
    uint8_t r[7] = { 0x09, 0x05, 0x14, 0x30, 5, 0x10, 0x26 };
    int64_t t = 0; bool os = true;
    TEST_ASSERT_EQUAL(EL_OK, el_hw_pcf85063_decode(r, &t, &os));
    TEST_ASSERT_EQUAL_INT64(1793369109, t);
    TEST_ASSERT_FALSE(os);
    r[0] |= EL_PCF85063_SECONDS_OS;                     /* oscillator stopped since last set */
    TEST_ASSERT_EQUAL(EL_OK, el_hw_pcf85063_decode(r, &t, &os));
    TEST_ASSERT_TRUE(os);
    TEST_ASSERT_EQUAL_INT64(1793369109, t);
    /* Undefined high bits in minutes/hours/days/months are masked. */
    uint8_t junk[7] = { 0x09, 0x85, 0xD4, 0xF0, 0xFD, 0xF0, 0x26 };
    TEST_ASSERT_EQUAL(EL_OK, el_hw_pcf85063_decode(junk, &t, &os));
    TEST_ASSERT_EQUAL_INT64(1793369109, t);
}

static void test_decode_rejects_garbage(void)
{
    int64_t t = 77; bool os = false;
    uint8_t bad_bcd[7]   = { 0x0A, 0x00, 0x00, 0x01, 0, 0x01, 0x26 };
    uint8_t bad_month[7] = { 0x00, 0x00, 0x00, 0x01, 0, 0x13, 0x26 };
    uint8_t feb30[7]     = { 0x00, 0x00, 0x00, 0x30, 0, 0x02, 0x24 };
    uint8_t hour24[7]    = { 0x00, 0x00, 0x24, 0x01, 0, 0x01, 0x26 };
    uint8_t day0[7]      = { 0x00, 0x00, 0x00, 0x00, 0, 0x01, 0x26 };
    uint8_t erased[7]    = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };   /* bus floating high */
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_hw_pcf85063_decode(bad_bcd, &t, &os));
    TEST_ASSERT_EQUAL(EL_ERR_RANGE,  el_hw_pcf85063_decode(bad_month, &t, &os));
    TEST_ASSERT_EQUAL(EL_ERR_RANGE,  el_hw_pcf85063_decode(feb30, &t, &os));
    TEST_ASSERT_EQUAL(EL_ERR_RANGE,  el_hw_pcf85063_decode(hour24, &t, &os));
    TEST_ASSERT_EQUAL(EL_ERR_RANGE,  el_hw_pcf85063_decode(day0, &t, &os));
    TEST_ASSERT_NOT_EQUAL(EL_OK,     el_hw_pcf85063_decode(erased, &t, &os));
    TEST_ASSERT_TRUE(os);
    TEST_ASSERT_EQUAL_INT64(77, t);
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_hw_pcf85063_decode(NULL, &t, &os));
}

static void test_power_on_reset_value_decodes(void)
{
    /* Datasheet table 7 reset values: seconds 0x80 (OS set), days 0x01, months 0x01, rest 0
     * -> 2000-01-01T00:00:00 with the OS flag: a fresh chip says "time invalid". */
    uint8_t r[7] = { 0x80, 0x00, 0x00, 0x01, 0x06, 0x01, 0x00 };
    int64_t t; bool os;
    TEST_ASSERT_EQUAL(EL_OK, el_hw_pcf85063_decode(r, &t, &os));
    TEST_ASSERT_TRUE(os);
    TEST_ASSERT_EQUAL_INT64(EL_PCF85063_UNIX_MIN, t);
}

static void test_leap_days_and_range(void)
{
    uint8_t r[7]; int64_t t; bool os;
    TEST_ASSERT_EQUAL(EL_OK, el_hw_pcf85063_encode(1709251199, r));   /* 2024-02-29T23:59:59 */
    TEST_ASSERT_EQUAL_HEX8(0x29, r[3]);
    TEST_ASSERT_EQUAL_HEX8(0x02, r[5]);
    TEST_ASSERT_EQUAL_HEX8(0x24, r[6]);
    TEST_ASSERT_EQUAL(EL_OK, el_hw_pcf85063_decode(r, &t, &os));
    TEST_ASSERT_EQUAL_INT64(1709251199, t);
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_hw_pcf85063_encode(EL_PCF85063_UNIX_MIN - 1, r));
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_hw_pcf85063_encode(EL_PCF85063_UNIX_MAX + 1, r));
    TEST_ASSERT_EQUAL(EL_OK, el_hw_pcf85063_encode(EL_PCF85063_UNIX_MAX, r));
    TEST_ASSERT_EQUAL_HEX8(0x99, r[6]);
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_hw_pcf85063_encode(0, NULL));
}

static void test_round_trip_2000_to_2099(void)
{
    for (int64_t t = EL_PCF85063_UNIX_MIN; t <= EL_PCF85063_UNIX_MAX; t += 86400 * 3 + 3607) {
        uint8_t r[7]; int64_t back; bool os;
        TEST_ASSERT_EQUAL(EL_OK, el_hw_pcf85063_encode(t, r));
        TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)(r[0] & EL_PCF85063_SECONDS_OS));
        TEST_ASSERT_EQUAL_UINT8(el_hw_weekday(t), r[4]);
        TEST_ASSERT_EQUAL(EL_OK, el_hw_pcf85063_decode(r, &back, &os));
        TEST_ASSERT_EQUAL_INT64(t, back);
    }
}

static void test_control_constants(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x01, EL_PCF85063_CTRL1_INIT);    /* 12.5 pF, 24 h, running */
    TEST_ASSERT_EQUAL_HEX8(0x00, EL_PCF85063_CTRL1_INIT & (EL_PCF85063_CTRL1_STOP | EL_PCF85063_CTRL1_12_24));
    TEST_ASSERT_EQUAL_HEX8(0x58, EL_PCF85063_SOFT_RESET);
}

void run_hw_pcf85063_tests(void)
{
    RUN_TEST(test_bcd);
    RUN_TEST(test_weekday);
    RUN_TEST(test_encode_known_date);
    RUN_TEST(test_decode_known_date_and_os_flag);
    RUN_TEST(test_decode_rejects_garbage);
    RUN_TEST(test_power_on_reset_value_decodes);
    RUN_TEST(test_leap_days_and_range);
    RUN_TEST(test_round_trip_2000_to_2099);
    RUN_TEST(test_control_constants);
}
