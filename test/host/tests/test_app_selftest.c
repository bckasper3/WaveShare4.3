#include <math.h>
#include <string.h>
#include "unity.h"
#include "el_app_selftest.h"

static int ran[8];
static void r_ok(void *c, el_test_report_t *r)   { (void)c; ran[0]++; el_test_set(r, EL_TEST_PASS, NAN, "ok"); }
static void r_fail(void *c, el_test_report_t *r) { (void)c; ran[1]++; el_test_set(r, EL_TEST_FAIL, NAN, "stuck"); }

static const el_test_def_t STORAGE_T[] = {
    { "NVS",     "NVS",     "", NULL, EL_TG_BOOT, r_ok },
    { "STORAGE", "Storage", "", NULL, EL_TG_BOOT, r_ok },
};
static const el_test_def_t BOARD_T[] = {
    { "I2C_BUS", "I2C bus",   "", NULL,      EL_TG_BOOT,              r_ok },
    { "RTC",     "RTC",       "", "I2C_BUS", EL_TG_BOOT,              r_ok },
    { "PSRAM",   "PSRAM",     "MB", NULL,     EL_TG_FULL,             r_ok },
    { "TOUCH_CORNERS", "Touch corners", "", "TOUCH", EL_TG_INTERACTIVE, r_ok },  /* TOUCH comes later */
};
static const el_test_def_t USB_T[] = {
    { "SD_CARD", "SD card", "", "I2C_BUS", EL_TG_BOOT,    r_ok },
    { "SD_WRITE", "SD write",  "", "SD_CARD", EL_TG_FIXTURE, r_ok },
};
static const el_test_def_t APP_T[] = {
    { "NVS",          "fallback NVS", "", NULL,     EL_TG_BOOT, r_fail },   /* duplicate: dropped */
    { "SENSOR",       "Sensor",       "", "I2C_BUS", EL_TG_BOOT, r_ok },
    { "SENSOR_NOISE", "Sensor noise", "%", "SENSOR", EL_TG_FULL, r_ok },
    { "",             "no id",        "", NULL,     EL_TG_BOOT, r_ok },
    { "NORUN",        "no fn",        "", NULL,     EL_TG_BOOT, NULL },
};

static el_test_def_t merged[EL_SELFTEST_MAX];
static size_t nm;

static void merge_all(void)
{
    el_app_test_table_t T[] = {
        { STORAGE_T, 2 }, { BOARD_T, 4 }, { NULL, 3 }, { USB_T, 2 }, { APP_T, 5 },
    };
    el_app_merge_report_t rep;
    TEST_ASSERT_EQUAL(EL_OK, el_app_selftest_merge(T, 5, merged, EL_SELFTEST_MAX, &nm, &rep));
    TEST_ASSERT_EQUAL_UINT16(1, rep.duplicates);
    TEST_ASSERT_EQUAL_UINT16(1, rep.deps_cleared);
    TEST_ASSERT_EQUAL_UINT16(2, rep.invalid);
    TEST_ASSERT_FALSE(rep.truncated);
}

static void test_merge_keeps_order_drops_duplicates_and_validates(void)
{
    merge_all();
    TEST_ASSERT_EQUAL_size_t(10, nm);
    TEST_ASSERT_EQUAL_STRING("NVS", merged[0].id);
    TEST_ASSERT_TRUE(merged[0].run == r_ok);              /* storage's NVS won */
    TEST_ASSERT_EQUAL_STRING("SENSOR", merged[8].id);
    TEST_ASSERT_EQUAL_STRING("I2C_BUS", merged[8].depends_on);
    TEST_ASSERT_NULL(merged[5].depends_on);               /* TOUCH_CORNERS: unknown dep cleared */
    TEST_ASSERT_EQUAL(EL_OK, el_selftest_validate(merged, nm));
    memset(ran, 0, sizeof ran);
    el_test_summary_t s;
    TEST_ASSERT_EQUAL(EL_OK, el_selftest_run(merged, nm, EL_TG_BOOT, NULL, NULL, NULL, &s));
    TEST_ASSERT_EQUAL_UINT16(6, s.count[EL_TEST_PASS]);
    TEST_ASSERT_EQUAL_INT(0, ran[1]);
}

static void test_merge_truncates_and_rejects_bad_args(void)
{
    el_app_test_table_t T[] = { { BOARD_T, 4 }, { USB_T, 2 } };
    el_test_def_t out[3];
    size_t n = 99;
    el_app_merge_report_t rep;
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_app_selftest_merge(T, 2, out, 3, &n, &rep));
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_TRUE(rep.truncated);
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_app_selftest_merge(T, 2, NULL, 3, &n, NULL));
    TEST_ASSERT_EQUAL(EL_OK, el_app_selftest_merge(NULL, 0, out, 3, &n, NULL));
    TEST_ASSERT_EQUAL_size_t(0, n);
}

static void test_select_by_id_brings_dependency_chain(void)
{
    merge_all();
    el_test_def_t sel[8];
    size_t n = 0;
    TEST_ASSERT_EQUAL(EL_OK, el_app_selftest_select(merged, nm, "sd_write", sel, 8, &n));
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_STRING("I2C_BUS", sel[0].id);
    TEST_ASSERT_EQUAL_STRING("SD_CARD", sel[1].id);
    TEST_ASSERT_EQUAL_STRING("SD_WRITE", sel[2].id);
    TEST_ASSERT_EQUAL(EL_OK, el_selftest_validate(sel, n));
    TEST_ASSERT_EQUAL(EL_OK, el_app_selftest_select(merged, nm, "PSRAM", sel, 8, &n));
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_app_selftest_select(merged, nm, "SENSOR_NOISE", sel, 2, &n));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_app_selftest_select(merged, nm, "NOPE", sel, 8, &n));
    TEST_ASSERT_EQUAL_INT(-1, el_app_selftest_find(merged, nm, NULL));
}

static void test_select_survives_a_dependency_cycle(void)
{
    static const el_test_def_t CYC[] = {
        { "A", "a", "", "B", EL_TG_BOOT, r_ok },
        { "B", "b", "", "A", EL_TG_BOOT, r_ok },
    };
    el_test_def_t sel[4];
    size_t n = 0;
    TEST_ASSERT_EQUAL(EL_OK, el_app_selftest_select(CYC, 2, "A", sel, 4, &n));
    TEST_ASSERT_EQUAL_size_t(2, n);
}

static void test_group_words_and_def_format(void)
{
    TEST_ASSERT_EQUAL_HEX32(EL_TG_BOOT, el_app_selftest_group_mask("boot"));
    TEST_ASSERT_EQUAL_HEX32(EL_TG_BOOT | EL_TG_FULL, el_app_selftest_group_mask("FULL"));
    TEST_ASSERT_EQUAL_HEX32(EL_TG_FIXTURE, el_app_selftest_group_mask("Fixture"));
    TEST_ASSERT_EQUAL_HEX32(EL_TG_INTERACTIVE, el_app_selftest_group_mask("INTERACTIVE"));
    TEST_ASSERT_EQUAL_HEX32(0, el_app_selftest_group_mask("I2C_BUS"));
    TEST_ASSERT_EQUAL_HEX32(0, el_app_selftest_group_mask(NULL));
    char b[96];
    static const el_test_def_t D = { "SENSOR", "Sensor reading", "C", "I2C_BUS", EL_TG_BOOT | EL_TG_FULL, r_ok };
    TEST_ASSERT_EQUAL(EL_OK, el_app_selftest_format_def(b, sizeof b, &D));
    TEST_ASSERT_EQUAL_STRING("TESTDEF SENSOR BOOT|FULL I2C_BUS Sensor reading", b);
    TEST_ASSERT_EQUAL(EL_OK, el_app_selftest_format_def(b, sizeof b, &STORAGE_T[0]));
    TEST_ASSERT_EQUAL_STRING("TESTDEF NVS BOOT - NVS", b);
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_app_selftest_format_def(b, 10, &D));
}

void run_app_selftest_tests(void)
{
    RUN_TEST(test_merge_keeps_order_drops_duplicates_and_validates);
    RUN_TEST(test_merge_truncates_and_rejects_bad_args);
    RUN_TEST(test_select_by_id_brings_dependency_chain);
    RUN_TEST(test_select_survives_a_dependency_cycle);
    RUN_TEST(test_group_words_and_def_format);
}
