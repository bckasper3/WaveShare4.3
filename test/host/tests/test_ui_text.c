#include <string.h>
#include "unity.h"
#include "el_ui_text.h"
#include "el_result.h"

static void test_charsets(void)
{
    TEST_ASSERT_TRUE(el_ui_text_char_ok(' ', EL_UI_CS_TEXT));
    TEST_ASSERT_FALSE(el_ui_text_char_ok('\n', EL_UI_CS_TEXT));
    TEST_ASSERT_FALSE(el_ui_text_char_ok((char)0xC3, EL_UI_CS_TEXT));   /* no UTF-8 in records */
    TEST_ASSERT_TRUE(el_ui_text_char_ok('-', EL_UI_CS_ID));
    TEST_ASSERT_FALSE(el_ui_text_char_ok(' ', EL_UI_CS_ID));
    TEST_ASSERT_FALSE(el_ui_text_char_ok(',', EL_UI_CS_ID));
    TEST_ASSERT_FALSE(el_ui_text_char_ok('a', EL_UI_CS_DIGITS));
    TEST_ASSERT_FALSE(el_ui_text_char_ok('1', (el_ui_charset_t)9));
}

/* A tag field is EL_TAG_LEN bytes on flash: at most 19 characters may be typed. */
static void test_append_respects_record_limit(void)
{
    char id[EL_TAG_LEN] = "";
    for (int i = 0; i < 19; i++) TEST_ASSERT_EQUAL(EL_OK, el_ui_text_append(id, sizeof id, 0, 'A', EL_UI_CS_ID));
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_ui_text_append(id, sizeof id, 0, 'A', EL_UI_CS_ID));
    TEST_ASSERT_EQUAL_size_t(19, strlen(id));
    char pin[9] = "";
    for (int i = 0; i < 4; i++) el_ui_text_append(pin, sizeof pin, 4, '7', EL_UI_CS_DIGITS);
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_ui_text_append(pin, sizeof pin, 4, '7', EL_UI_CS_DIGITS));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_ui_text_append(pin, sizeof pin, 0, 'x', EL_UI_CS_DIGITS));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_ui_text_append(NULL, 4, 0, '1', EL_UI_CS_DIGITS));
    TEST_ASSERT_EQUAL_STRING("7777", pin);
}

static void test_number_rules(void)
{
    char b[12] = "";
    TEST_ASSERT_EQUAL(EL_OK, el_ui_text_append(b, sizeof b, 0, '-', EL_UI_CS_DECIMAL));
    TEST_ASSERT_EQUAL(EL_OK, el_ui_text_append(b, sizeof b, 0, '0', EL_UI_CS_DECIMAL));
    TEST_ASSERT_EQUAL(EL_OK, el_ui_text_append(b, sizeof b, 0, '.', EL_UI_CS_DECIMAL));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_ui_text_append(b, sizeof b, 0, '.', EL_UI_CS_DECIMAL));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_ui_text_append(b, sizeof b, 0, '-', EL_UI_CS_DECIMAL));
    TEST_ASSERT_EQUAL(EL_OK, el_ui_text_append(b, sizeof b, 0, '5', EL_UI_CS_DECIMAL));
    TEST_ASSERT_EQUAL_STRING("-0.5", b);
    el_ui_text_backspace(b); TEST_ASSERT_EQUAL_STRING("-0.", b);
    char e[2] = ""; el_ui_text_backspace(e); TEST_ASSERT_EQUAL_STRING("", e);
    el_ui_text_backspace(NULL);
}

void run_ui_text_tests(void)
{
    RUN_TEST(test_charsets);
    RUN_TEST(test_append_respects_record_limit);
    RUN_TEST(test_number_rules);
}
