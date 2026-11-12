#include <string.h>
#include "unity.h"
#include "el_ui_status.h"

/* WCAG relative luminance contrast, to prove on_fg text is readable on every strong colour. */
static double lin(double c) { c /= 255.0; return c <= 0.03928 ? c / 12.92 : __builtin_pow((c + 0.055) / 1.055, 2.4); }
static double lum(uint32_t rgb)
{
    return 0.2126 * lin((rgb >> 16) & 0xFF) + 0.7152 * lin((rgb >> 8) & 0xFF) + 0.0722 * lin(rgb & 0xFF);
}
static double contrast(uint32_t a, uint32_t b)
{
    double la = lum(a), lb = lum(b);
    return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

static void test_every_status_has_tone_label_and_glyph(void)
{
    TEST_ASSERT_EQUAL(EL_UI_TONE_OK, el_ui_status_tone(EL_RES_NORMAL));
    TEST_ASSERT_EQUAL(EL_UI_TONE_BAD, el_ui_status_tone(EL_RES_ALARM));
    TEST_ASSERT_EQUAL(EL_UI_TONE_FAULT, el_ui_status_tone(EL_RES_ERROR));
    TEST_ASSERT_EQUAL(EL_UI_TONE_NEUTRAL, el_ui_status_tone(EL_RES_NONE));
    TEST_ASSERT_EQUAL(EL_UI_TONE_NEUTRAL, el_ui_status_tone((el_result_status_t)77));
    /* never colour alone: every real status has a glyph and a label */
    for (int s = EL_RES_NORMAL; s <= EL_RES_ERROR; s++) {
        TEST_ASSERT_NOT_EQUAL(EL_UI_GLYPH_NONE, el_ui_status_glyph((el_result_status_t)s));
        TEST_ASSERT_TRUE(strlen(el_ui_status_label((el_result_status_t)s)) > 0);
    }
    TEST_ASSERT_EQUAL_STRING("ALARM", el_ui_status_label(EL_RES_ALARM));
    TEST_ASSERT_EQUAL_STRING("NO RESULT", el_ui_status_label(EL_RES_NONE));
}

static void test_tone_colours_have_readable_contrast(void)
{
    for (int dark = 0; dark < 2; dark++) {
        for (int t = 0; t < EL_UI_TONE_COUNT; t++) {
            el_ui_tone_colors_t c;
            el_ui_tone_colors((el_ui_tone_t)t, dark != 0, &c);
            TEST_ASSERT_TRUE_MESSAGE(contrast(c.on_fg, c.fg) >= 4.5, "on_fg on fg");
            TEST_ASSERT_TRUE_MESSAGE(contrast(c.text, c.tint) >= 4.5, "text on tint");
        }
    }
    el_ui_tone_colors_t a, b;
    el_ui_tone_colors((el_ui_tone_t)99, false, &a);
    el_ui_tone_colors(EL_UI_TONE_NEUTRAL, false, &b);
    TEST_ASSERT_EQUAL_UINT32(b.fg, a.fg);
    el_ui_tone_colors(EL_UI_TONE_OK, false, NULL);     /* no crash */
}

static void test_fault_and_reject_texts_cover_every_code(void)
{
    for (int f = 0; f <= EL_MEAS_FAULT_COUNT; f++) {
        TEST_ASSERT_NOT_NULL(el_ui_fault_title((el_meas_fault_t)f));
        TEST_ASSERT_TRUE(strlen(el_ui_fault_hint((el_meas_fault_t)f)) > 5);
    }
    for (int r = 0; r <= EL_MEAS_REJECT_COUNT; r++)
        TEST_ASSERT_TRUE(strlen(el_ui_reject_text((el_meas_reject_t)r)) > 5);
    TEST_ASSERT_EQUAL_STRING("Sensor not responding", el_ui_fault_title(EL_MEAS_FAULT_SENSOR_NO_ACK));
}

static void test_alarm_texts(void)
{
    TEST_ASSERT_EQUAL_STRING("", el_ui_alarm_text(0));
    TEST_ASSERT_EQUAL_STRING("Temp. high", el_ui_alarm_text(EL_ALARM_T_HIGH));
    TEST_ASSERT_EQUAL_STRING("Temp. low", el_ui_alarm_text(EL_ALARM_T_LOW));
    TEST_ASSERT_EQUAL_STRING("Humidity high", el_ui_alarm_text(EL_ALARM_RH_HIGH));
    TEST_ASSERT_EQUAL_STRING("Humidity low", el_ui_alarm_text(EL_ALARM_RH_LOW));
    TEST_ASSERT_EQUAL_STRING("Temp. and RH", el_ui_alarm_text(EL_ALARM_T_LOW | EL_ALARM_RH_HIGH));
}

static void test_badges(void)
{
    el_ui_badge_t b;
    el_ui_level_badge(EL_LEVEL_ENGINEER, &b);
    TEST_ASSERT_EQUAL_STRING("ENGINEER", b.text); TEST_ASSERT_EQUAL(EL_UI_TONE_FAULT, b.tone);
    el_ui_level_badge(EL_LEVEL_ADMIN, &b);
    TEST_ASSERT_EQUAL_STRING("ADMIN", b.text); TEST_ASSERT_EQUAL(EL_UI_TONE_ACCENT, b.tone);
    el_ui_level_badge((el_level_t)42, &b);
    TEST_ASSERT_EQUAL_STRING("OPERATOR", b.text);
}

void run_ui_status_tests(void)
{
    RUN_TEST(test_every_status_has_tone_label_and_glyph);
    RUN_TEST(test_tone_colours_have_readable_contrast);
    RUN_TEST(test_fault_and_reject_texts_cover_every_code);
    RUN_TEST(test_alarm_texts);
    RUN_TEST(test_badges);
}
