#include "unity.h"
#include "el_hw_ch422g.h"

static void test_boot_defaults_are_safe(void)
{
    el_hw_ch422g_t s;
    el_hw_ch422g_boot(&s);
    /* OC = 0x03: DO0 OFF, DO1 OFF. */
    TEST_ASSERT_EQUAL_HEX8(0x03, el_hw_ch422g_oc_byte(&s));
    TEST_ASSERT_FALSE(el_hw_ch422g_oc_is_on(&s, EL_OC_DO0));
    TEST_ASSERT_FALSE(el_hw_ch422g_oc_is_on(&s, EL_OC_DO1));
    /* IO: SD CS high only; backlight off, touch + LCD in reset, DI bits 0. */
    TEST_ASSERT_EQUAL_HEX8(0x10, el_hw_ch422g_io_byte(&s));
    TEST_ASSERT_FALSE(el_hw_ch422g_io_get(&s, EL_EXIO_DISP));
    TEST_ASSERT_FALSE(el_hw_ch422g_io_get(&s, EL_EXIO_TP_RST));
    TEST_ASSERT_TRUE(el_hw_ch422g_io_get(&s, EL_EXIO_SD_CS));
}

static void test_oc_polarity_is_inverted(void)
{
    el_hw_ch422g_t s;
    el_hw_ch422g_boot(&s);
    TEST_ASSERT_EQUAL(EL_OK, el_hw_ch422g_oc_set(&s, EL_OC_DO0, true));    /* DO0 output on */
    TEST_ASSERT_EQUAL_HEX8(0x02, el_hw_ch422g_oc_byte(&s));
    TEST_ASSERT_EQUAL(EL_OK, el_hw_ch422g_oc_set(&s, EL_OC_DO1, true));
    TEST_ASSERT_EQUAL_HEX8(0x00, el_hw_ch422g_oc_byte(&s));                /* the 0-byte write case */
    TEST_ASSERT_TRUE(el_hw_ch422g_oc_is_on(&s, EL_OC_DO1));
    TEST_ASSERT_EQUAL(EL_OK, el_hw_ch422g_oc_set(&s, EL_OC_DO0, false));
    TEST_ASSERT_EQUAL_HEX8(0x01, el_hw_ch422g_oc_byte(&s));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_hw_ch422g_oc_set(&s, 4, true));
    TEST_ASSERT_EQUAL_HEX8(0x01, el_hw_ch422g_oc_byte(&s));
    TEST_ASSERT_FALSE(el_hw_ch422g_oc_is_on(&s, 9));
}

static void test_di_bits_can_never_be_driven(void)
{
    el_hw_ch422g_t s;
    el_hw_ch422g_boot(&s);
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_hw_ch422g_io_set(&s, EL_EXIO_DI0, true));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_hw_ch422g_io_set(&s, EL_EXIO_DI1, true));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_hw_ch422g_io_set(&s, EL_EXIO_DI1, false));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_hw_ch422g_io_set(&s, 8, true));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_hw_ch422g_io_set(NULL, 2, true));
    /* Even a corrupted shadow is sanitised on the way out. */
    s.io = 0xFF;
    TEST_ASSERT_EQUAL_HEX8(0xDE, el_hw_ch422g_io_byte(&s));
}

static void test_io_set_and_clear(void)
{
    el_hw_ch422g_t s;
    el_hw_ch422g_boot(&s);
    TEST_ASSERT_EQUAL(EL_OK, el_hw_ch422g_io_set(&s, EL_EXIO_DISP, true));
    TEST_ASSERT_EQUAL(EL_OK, el_hw_ch422g_io_set(&s, EL_EXIO_TP_RST, true));
    TEST_ASSERT_EQUAL(EL_OK, el_hw_ch422g_io_set(&s, EL_EXIO_LCD_RST, true));
    TEST_ASSERT_EQUAL_HEX8(0x1E, el_hw_ch422g_io_byte(&s));   /* Waveshare bl_on() value, IO5 = 0 */
    TEST_ASSERT_EQUAL(EL_OK, el_hw_ch422g_io_set(&s, EL_EXIO_SD_CS, false));
    TEST_ASSERT_EQUAL_HEX8(0x0E, el_hw_ch422g_io_byte(&s));
    TEST_ASSERT_EQUAL(EL_OK, el_hw_ch422g_io_set(&s, EL_EXIO_DISP, false));
    TEST_ASSERT_EQUAL_HEX8(0x0A, el_hw_ch422g_io_byte(&s));   /* Waveshare SD demo value */
}

static void test_di_decode_is_inverted(void)
{
    bool d0, d1;
    el_hw_ch422g_di_decode(0xFF, &d0, &d1);          /* both idle (pull-ups) */
    TEST_ASSERT_FALSE(d0); TEST_ASSERT_FALSE(d1);
    el_hw_ch422g_di_decode(0xFE, &d0, &d1);          /* DI0 opto conducting */
    TEST_ASSERT_TRUE(d0);  TEST_ASSERT_FALSE(d1);
    el_hw_ch422g_di_decode(0xDF, &d0, &d1);
    TEST_ASSERT_FALSE(d0); TEST_ASSERT_TRUE(d1);
    el_hw_ch422g_di_decode(0x00, &d0, NULL);         /* NULL output tolerated */
    TEST_ASSERT_TRUE(d0);
}

static void test_readback_ignores_di_bits(void)
{
    el_hw_ch422g_t s;
    el_hw_ch422g_boot(&s);
    el_hw_ch422g_io_set(&s, EL_EXIO_DISP, true);     /* shadow 0x14 */
    TEST_ASSERT_TRUE(el_hw_ch422g_readback_ok(&s, 0x14));
    TEST_ASSERT_TRUE(el_hw_ch422g_readback_ok(&s, 0x35));    /* DI bits differ: ignored */
    TEST_ASSERT_FALSE(el_hw_ch422g_readback_ok(&s, 0x10));   /* backlight bit lost */
    TEST_ASSERT_FALSE(el_hw_ch422g_readback_ok(&s, 0x94));   /* spare bit 7 stuck high */
}

static void test_pattern_only_touches_spare_bits(void)
{
    el_hw_ch422g_t s;
    el_hw_ch422g_boot(&s);
    el_hw_ch422g_io_set(&s, EL_EXIO_DISP, true);
    el_hw_ch422g_io_set(&s, EL_EXIO_TP_RST, true);
    uint8_t base = el_hw_ch422g_io_byte(&s);
    for (unsigned n = 0; n < 4; n++) {
        uint8_t p = el_hw_ch422g_test_pattern(&s, n);
        TEST_ASSERT_EQUAL_HEX8(base, (uint8_t)(p & 0x3F));
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(n << 6), (uint8_t)(p & 0xC0));
        TEST_ASSERT_EQUAL_HEX8(0, (uint8_t)(p & EL_CH422G_IO_DI_MASK));
    }
}

static void test_raw_names(void)
{
    el_hw_io_kind_t k; uint8_t b;
    TEST_ASSERT_EQUAL(EL_OK, el_hw_ch422g_parse_name("DO0", &k, &b));
    TEST_ASSERT_EQUAL(EL_HW_IO_OC, k); TEST_ASSERT_EQUAL(EL_OC_DO0, b);
    TEST_ASSERT_EQUAL(EL_OK, el_hw_ch422g_parse_name("do1", &k, &b));
    TEST_ASSERT_EQUAL(EL_OC_DO1, b);
    TEST_ASSERT_EQUAL(EL_OK, el_hw_ch422g_parse_name("Bl", &k, &b));
    TEST_ASSERT_EQUAL(EL_HW_IO_EXIO, k); TEST_ASSERT_EQUAL(EL_EXIO_DISP, b);
    TEST_ASSERT_EQUAL(EL_OK, el_hw_ch422g_parse_name("tp_rst", &k, &b));
    TEST_ASSERT_EQUAL(EL_EXIO_TP_RST, b);
    TEST_ASSERT_EQUAL(EL_OK, el_hw_ch422g_parse_name("LCD_RST", &k, &b));
    TEST_ASSERT_EQUAL(EL_EXIO_LCD_RST, b);
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_hw_ch422g_parse_name("DI0", &k, &b));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_hw_ch422g_parse_name("DI1", &k, &b));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_hw_ch422g_parse_name("EXIO5", &k, &b));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_hw_ch422g_parse_name("USB_SEL", &k, &b));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_hw_ch422g_parse_name("DO", &k, &b));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_hw_ch422g_parse_name("DO00", &k, &b));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_hw_ch422g_parse_name("", &k, &b));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_hw_ch422g_parse_name(NULL, &k, &b));
}

void run_hw_ch422g_tests(void)
{
    RUN_TEST(test_boot_defaults_are_safe);
    RUN_TEST(test_oc_polarity_is_inverted);
    RUN_TEST(test_di_bits_can_never_be_driven);
    RUN_TEST(test_io_set_and_clear);
    RUN_TEST(test_di_decode_is_inverted);
    RUN_TEST(test_readback_ignores_di_bits);
    RUN_TEST(test_pattern_only_touches_spare_bits);
    RUN_TEST(test_raw_names);
}
