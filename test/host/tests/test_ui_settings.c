#include <string.h>
#include "unity.h"
#include "el_ui_settings.h"

static void test_every_setting_is_on_exactly_one_page(void)
{
    size_t total = 0;
    for (int g = 0; g < EL_UI_SG_COUNT; g++) {
        size_t n = el_ui_set_group_count((el_ui_set_group_t)g);
        total += n;
        for (size_t i = 0; i < n; i++) {
            el_set_id_t id = el_ui_set_group_item((el_ui_set_group_t)g, i);
            TEST_ASSERT_TRUE(id < EL_SET_COUNT);
            TEST_ASSERT_EQUAL(g, el_ui_set_group(id));
        }
        TEST_ASSERT_EQUAL(EL_SET_COUNT, el_ui_set_group_item((el_ui_set_group_t)g, n));
    }
    TEST_ASSERT_EQUAL_size_t(EL_SET_COUNT, total);
}

static void test_titles_are_human_and_never_null(void)
{
    for (int i = 0; i < EL_SET_COUNT; i++) {
        const char *t = el_ui_set_title((el_set_id_t)i);
        TEST_ASSERT_NOT_NULL(t);
        TEST_ASSERT_NULL_MESSAGE(strchr(t, '_'), t);         /* every schema key has wording */
    }
    TEST_ASSERT_EQUAL_STRING("Readings per result", el_ui_set_title(EL_SET_REPEATS));
    TEST_ASSERT_EQUAL_STRING("?", el_ui_set_title(EL_SET_COUNT));
    TEST_ASSERT_EQUAL_STRING("Device", el_ui_set_group_name(EL_UI_SG_DEVICE));
    TEST_ASSERT_EQUAL_STRING("Other", el_ui_set_group_name(EL_UI_SG_COUNT));
}

static void test_editors_follow_schema_types(void)
{
    TEST_ASSERT_EQUAL(EL_UI_ED_TOGGLE, el_ui_set_editor(EL_SET_SERIAL_STREAM));
    TEST_ASSERT_EQUAL(EL_UI_ED_CHOICE, el_ui_set_editor(EL_SET_MB_BAUD));
    TEST_ASSERT_EQUAL(EL_UI_ED_INT, el_ui_set_editor(EL_SET_REPEATS));
    TEST_ASSERT_EQUAL(EL_UI_ED_FLOAT, el_ui_set_editor(EL_SET_T_OFFSET));
    TEST_ASSERT_EQUAL(EL_UI_ED_PIN, el_ui_set_editor(EL_SET_ENGINEER_CODE));
    TEST_ASSERT_EQUAL(EL_UI_ED_SECRET, el_ui_set_editor(EL_SET_WIFI_PASSWORD));
    TEST_ASSERT_EQUAL(EL_UI_ED_PIN, el_ui_set_editor(EL_SET_ADMIN_PIN));
    TEST_ASSERT_EQUAL(EL_UI_ED_TEXT, el_ui_set_editor(EL_SET_OPERATOR_ID));
}

static void test_value_texts(void)
{
    el_settings_t s; char b[40];
    el_settings_defaults(&s);
    el_ui_set_value_text(&s, EL_SET_REPEATS, b, sizeof b);       TEST_ASSERT_EQUAL_STRING("3", b);
    el_ui_set_value_text(&s, EL_SET_IDLE_DIM, b, sizeof b);      TEST_ASSERT_EQUAL_STRING("120 s", b);
    el_ui_set_value_text(&s, EL_SET_T_HIGH, b, sizeof b);        TEST_ASSERT_EQUAL_STRING("35 \xC2\xB0" "C", b);
    el_ui_set_value_text(&s, EL_SET_RH_LOW, b, sizeof b);        TEST_ASSERT_EQUAL_STRING("20 %RH", b);
    el_ui_set_value_text(&s, EL_SET_LOG_INTERVAL, b, sizeof b);  TEST_ASSERT_EQUAL_STRING("5 min", b);
    el_ui_set_value_text(&s, EL_SET_SENSOR_TYPE, b, sizeof b);   TEST_ASSERT_EQUAL_STRING("SHT4x (SHT40/41/45)", b);
    el_ui_set_value_text(&s, EL_SET_LOCATION, b, sizeof b);      TEST_ASSERT_EQUAL_STRING("(none)", b);
    el_ui_set_value_text(&s, EL_SET_MB_BAUD, b, sizeof b);       TEST_ASSERT_EQUAL_STRING("19200", b);
    el_ui_set_value_text(&s, EL_SET_SERIAL_STREAM, b, sizeof b); TEST_ASSERT_EQUAL_STRING("Off", b);
    el_ui_set_value_text(&s, EL_SET_ADMIN_PIN, b, sizeof b);     TEST_ASSERT_EQUAL_STRING("Set", b);   /* factory default PIN */
    el_ui_set_value_text(&s, EL_SET_OPERATOR_ID, b, sizeof b);   TEST_ASSERT_EQUAL_STRING("(none)", b);
    el_ui_set_value_text(&s, EL_SET_UTC_OFFSET, b, sizeof b);    TEST_ASSERT_EQUAL_STRING("UTC+00:00", b);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_set_str(&s, EL_SET_ADMIN_PIN, "4321", EL_LEVEL_ADMIN));
    el_ui_set_value_text(&s, EL_SET_ADMIN_PIN, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("Set", b);                           /* never the PIN itself */
    TEST_ASSERT_EQUAL(EL_OK, el_settings_set_str(&s, EL_SET_IDLE_DIM, "0", EL_LEVEL_ADMIN));
    el_ui_set_value_text(&s, EL_SET_IDLE_DIM, b, sizeof b);      TEST_ASSERT_EQUAL_STRING("Never", b);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_set_str(&s, EL_SET_LOG_INTERVAL, "0", EL_LEVEL_ADMIN));
    el_ui_set_value_text(&s, EL_SET_LOG_INTERVAL, b, sizeof b);  TEST_ASSERT_EQUAL_STRING("Off", b);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_set_str(&s, EL_SET_T_LOW, "-40", EL_LEVEL_ADMIN));   /* range end = off */
    el_ui_set_value_text(&s, EL_SET_T_LOW, b, sizeof b);         TEST_ASSERT_EQUAL_STRING("Off", b);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_set_str(&s, EL_SET_RH_HIGH, "100", EL_LEVEL_ADMIN));
    el_ui_set_value_text(&s, EL_SET_RH_HIGH, b, sizeof b);       TEST_ASSERT_EQUAL_STRING("Off", b);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_set_str(&s, EL_SET_RH_HIGH, "30.5", EL_LEVEL_ADMIN));  /* not "0 = off" */
    el_ui_set_value_text(&s, EL_SET_RH_HIGH, b, sizeof b);       TEST_ASSERT_EQUAL_STRING("30.5 %RH", b);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_set_str(&s, EL_SET_UTC_OFFSET, "-330", EL_LEVEL_ADMIN));
    el_ui_set_value_text(&s, EL_SET_UTC_OFFSET, b, sizeof b);    TEST_ASSERT_EQUAL_STRING("UTC-05:30", b);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_set_str(&s, EL_SET_DO1_MODE, "BUSY", EL_LEVEL_ADMIN));
    el_ui_set_value_text(&s, EL_SET_DO1_MODE, b, sizeof b);      TEST_ASSERT_EQUAL_STRING("On while measuring", b);
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_ui_set_value_text(&s, EL_SET_COUNT, b, sizeof b));
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_ui_set_value_text(&s, EL_SET_IDLE_OFF, b, 4));
}

static void test_enum_texts_and_levels(void)
{
    TEST_ASSERT_EQUAL_STRING("On while in alarm", el_ui_set_enum_text(EL_SET_DO1_MODE, EL_DO1_ON_ALARM));
    TEST_ASSERT_EQUAL_STRING("0x45", el_ui_set_enum_text(EL_SET_SENSOR_ADDR, EL_SENSOR_ADDR_45));
    TEST_ASSERT_EQUAL_STRING("115200", el_ui_set_enum_text(EL_SET_MB_BAUD, EL_BAUD_115200));  /* schema name */
    TEST_ASSERT_NULL(el_ui_set_enum_text(EL_SET_DO1_MODE, 9));
    TEST_ASSERT_NULL(el_ui_set_enum_text(EL_SET_REPEATS, 0));
    TEST_ASSERT_TRUE(el_ui_set_writable(EL_SET_OPERATOR_ID, EL_LEVEL_OPERATOR));
    TEST_ASSERT_FALSE(el_ui_set_writable(EL_SET_REPEATS, EL_LEVEL_OPERATOR));
    TEST_ASSERT_TRUE(el_ui_set_writable(EL_SET_REPEATS, EL_LEVEL_ADMIN));
    TEST_ASSERT_FALSE(el_ui_set_writable(EL_SET_DEVICE_SERIAL, EL_LEVEL_ADMIN));
    TEST_ASSERT_TRUE(el_ui_set_writable(EL_SET_DEVICE_SERIAL, EL_LEVEL_ENGINEER));
    TEST_ASSERT_FALSE(el_ui_set_writable(EL_SET_T_OFFSET, EL_LEVEL_OPERATOR));
    TEST_ASSERT_TRUE(el_ui_set_writable(EL_SET_T_OFFSET, EL_LEVEL_ADMIN));
    TEST_ASSERT_FALSE(el_ui_set_writable(EL_SET_COUNT, EL_LEVEL_ENGINEER));
}

void run_ui_settings_tests(void)
{
    RUN_TEST(test_every_setting_is_on_exactly_one_page);
    RUN_TEST(test_titles_are_human_and_never_null);
    RUN_TEST(test_editors_follow_schema_types);
    RUN_TEST(test_value_texts);
    RUN_TEST(test_enum_texts_and_levels);
}
