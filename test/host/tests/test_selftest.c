#include <math.h>
#include <string.h>
#include "unity.h"
#include "el_selftest.h"
#include "el_i2c_check.h"
#include "el_board.h"

/* ---- a fake device the test functions "measure" ---- */
typedef struct { bool bus_ok; float supply_v; int ran; } fake_t;

static void t_bus(void *c, el_test_report_t *r)
{
    fake_t *f = c; f->ran++;
    if (f->bus_ok) el_test_set(r, EL_TEST_PASS, 6, "6 devices OK");
    else           el_test_set(r, EL_TEST_FAIL, NAN, "SDA stuck low");
}
static void t_supply(void *c, el_test_report_t *r)
{
    fake_t *f = c; f->ran++;
    static const el_test_limits_t L = { 3.0f, 3.45f, 4.25f, 4.35f };
    el_test_set(r, el_test_judge(f->supply_v, &L), f->supply_v, "bus voltage\nwith newline");
}
static void t_nvs(void *c, el_test_report_t *r)    { ((fake_t *)c)->ran++; el_test_set(r, EL_TEST_PASS, NAN, "ok"); }
static void t_silent(void *c, el_test_report_t *r) { ((fake_t *)c)->ran++; (void)r; }   /* forgets to report */
static void t_touch(void *c, el_test_report_t *r)  { ((fake_t *)c)->ran++; el_test_set(r, EL_TEST_PASS, NAN, "ok"); }

static const el_test_def_t TESTS[] = {
    { "NVS",     "NVS settings",       "",  NULL,      EL_TG_BOOT,              t_nvs },
    { "I2C_BUS", "I2C bus",            "",  NULL,      EL_TG_BOOT,              t_bus },
    { "SUPPLY",  "Supply voltage",    "V", "I2C_BUS", EL_TG_BOOT | EL_TG_FULL, t_supply },
    { "SILENT",  "Forgets to report",  "",  NULL,      EL_TG_FULL,              t_silent },
    { "TOUCH",   "Touch corners",      "",  "I2C_BUS", EL_TG_INTERACTIVE,       t_touch },
};
#define NT (sizeof TESTS / sizeof TESTS[0])

static char lines[8][128];
static int  nlines;
static void sink(void *u, const el_test_def_t *t, const el_test_report_t *r)
{
    (void)u;
    if (nlines < 8) el_selftest_format(lines[nlines++], sizeof lines[0], t, r);
}

static void test_runs_selected_group_in_order(void)
{
    fake_t f = { true, 3.9f, 0 }; el_test_summary_t s; nlines = 0;
    TEST_ASSERT_EQUAL(EL_OK, el_selftest_run(TESTS, NT, EL_TG_BOOT, &f, sink, NULL, &s));
    TEST_ASSERT_EQUAL(3, f.ran);
    TEST_ASSERT_EQUAL(3, nlines);
    TEST_ASSERT_EQUAL_STRING("TEST NVS PASS - - ok", lines[0]);
    TEST_ASSERT_EQUAL_STRING("TEST I2C_BUS PASS 6 - 6 devices OK", lines[1]);
    TEST_ASSERT_EQUAL_STRING("TEST SUPPLY PASS 3.9 V bus voltage with newline", lines[2]);
    TEST_ASSERT_EQUAL(3, s.count[EL_TEST_PASS]);
}

static void test_failed_dependency_skips_dependents(void)
{
    fake_t f = { false, 3.9f, 0 }; el_test_summary_t s; nlines = 0;
    el_selftest_run(TESTS, NT, EL_TG_BOOT, &f, sink, NULL, &s);
    TEST_ASSERT_EQUAL(2, f.ran);                       /* SUPPLY never touched the bus */
    TEST_ASSERT_EQUAL_STRING("TEST SUPPLY SKIP - V needs I2C_BUS", lines[2]);
    TEST_ASSERT_EQUAL(1, s.count[EL_TEST_FAIL]);
    TEST_ASSERT_EQUAL(1, s.count[EL_TEST_SKIP]);
}

static void test_unselected_dependency_does_not_block(void)
{
    fake_t f = { false, 3.9f, 0 }; nlines = 0;
    el_selftest_run(TESTS, NT, EL_TG_INTERACTIVE, &f, sink, NULL, NULL);
    TEST_ASSERT_EQUAL(1, f.ran);
    TEST_ASSERT_EQUAL_STRING("TEST TOUCH PASS - - ok", lines[0]);
}

static void test_test_that_does_not_report_is_a_fail(void)
{
    fake_t f = { true, 3.9f, 0 }; nlines = 0;
    el_selftest_run(TESTS, NT, EL_TG_FULL, &f, sink, NULL, NULL);
    TEST_ASSERT_EQUAL_STRING("TEST SILENT FAIL - - test did not report", lines[1]);
}

static void test_limits(void)
{
    const el_test_limits_t L = { 3.0f, 3.45f, 4.25f, 4.35f };
    TEST_ASSERT_EQUAL(EL_TEST_FAIL, el_test_judge(2.9f, &L));
    TEST_ASSERT_EQUAL(EL_TEST_WARN, el_test_judge(3.3f, &L));
    TEST_ASSERT_EQUAL(EL_TEST_PASS, el_test_judge(3.45f, &L));
    TEST_ASSERT_EQUAL(EL_TEST_PASS, el_test_judge(4.25f, &L));
    TEST_ASSERT_EQUAL(EL_TEST_WARN, el_test_judge(4.3f, &L));
    TEST_ASSERT_EQUAL(EL_TEST_FAIL, el_test_judge(4.4f, &L));
    TEST_ASSERT_EQUAL(EL_TEST_FAIL, el_test_judge(NAN, &L));
    const el_test_limits_t only_max = { NAN, NAN, NAN, 100.0f };
    TEST_ASSERT_EQUAL(EL_TEST_PASS, el_test_judge(-5.0f, &only_max));
    TEST_ASSERT_EQUAL(EL_TEST_FAIL, el_test_judge(101.0f, &only_max));
}

static void test_bad_tables_are_rejected(void)
{
    const el_test_def_t dup[] = { { "A", "", "", NULL, 1, t_nvs }, { "A", "", "", NULL, 1, t_nvs } };
    const el_test_def_t fwd[] = { { "B", "", "", "A", 1, t_nvs }, { "A", "", "", NULL, 1, t_nvs } };
    const el_test_def_t unk[] = { { "B", "", "", "Z", 1, t_nvs } };
    const el_test_def_t spc[] = { { "MY TEST", "", "", NULL, 1, t_nvs } };
    TEST_ASSERT_EQUAL(EL_OK, el_selftest_validate(TESTS, NT));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_selftest_validate(dup, 2));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_selftest_validate(fwd, 2));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_selftest_validate(unk, 1));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_selftest_validate(spc, 1));
}

static void test_summary_line(void)
{
    el_test_summary_t s = { { 10, 1, 2, 3 } };
    char b[64];
    TEST_ASSERT_EQUAL(EL_OK, el_selftest_format_summary(b, sizeof b, &s));
    TEST_ASSERT_EQUAL_STRING("SELFTEST END pass=10 warn=1 fail=2 skip=3", b);
}

/* ---- I2C bus check against the board table ---- */
static void scan_full_board(el_i2c_scan_t *s)
{
    el_i2c_scan_clear(s);
    el_i2c_scan_mark(s, 0x24); el_i2c_scan_mark(s, 0x38);    /* CH422G command addresses */
    el_i2c_scan_mark(s, 0x51); el_i2c_scan_mark(s, 0x5D);    /* RTC, touch */
    el_i2c_scan_mark(s, 0x44);                                /* SHT4x on the I2C terminal */
}

static void test_i2c_logger_without_sensor_warns(void)
{
    el_i2c_scan_t s; el_i2c_check_t c; char d[96];
    scan_full_board(&s);
    s.bits[0x44 / 8] &= (uint8_t)~(1u << (0x44 % 8));        /* bit order per el_i2c_scan_mark */
    el_i2c_check(&s, EL_BOARD_I2C_EXPECT, EL_BOARD_I2C_EXPECT_N, &c);
    TEST_ASSERT_EQUAL(EL_TEST_WARN, c.outcome);                 /* optional: runs on the simulator */
    el_i2c_check_describe(&c, EL_BOARD_I2C_EXPECT, EL_BOARD_I2C_EXPECT_N, d, sizeof d);
    TEST_ASSERT_EQUAL_STRING("missing SHT sensor(0x44)?", d);
}

static void test_i2c_full_board_passes(void)
{
    el_i2c_scan_t s; el_i2c_check_t c; char d[96];
    scan_full_board(&s);
    TEST_ASSERT_EQUAL(EL_OK, el_i2c_check(&s, EL_BOARD_I2C_EXPECT, EL_BOARD_I2C_EXPECT_N, &c));
    TEST_ASSERT_EQUAL(EL_TEST_PASS, c.outcome);
    el_i2c_check_describe(&c, EL_BOARD_I2C_EXPECT, EL_BOARD_I2C_EXPECT_N, d, sizeof d);
    TEST_ASSERT_EQUAL_STRING("5 devices OK", d);
    el_i2c_scan_clear(&s);                                     /* SHT3x with ADDR high answers at 0x45 */
    scan_full_board(&s);
    s.bits[0x44 / 8] &= (uint8_t)~(1u << (0x44 % 8));
    el_i2c_scan_mark(&s, 0x45);
    el_i2c_check(&s, EL_BOARD_I2C_EXPECT, EL_BOARD_I2C_EXPECT_N, &c);
    TEST_ASSERT_EQUAL(EL_TEST_PASS, c.outcome);
}

static void test_i2c_missing_parts_fail_and_are_named(void)
{
    /* touch controller strapped wrong and RTC dead: both required */
    el_i2c_scan_t s; el_i2c_check_t c; char d[96];
    el_i2c_scan_clear(&s);
    el_i2c_scan_mark(&s, 0x24); el_i2c_scan_mark(&s, 0x38);
    el_i2c_check(&s, EL_BOARD_I2C_EXPECT, EL_BOARD_I2C_EXPECT_N, &c);
    TEST_ASSERT_EQUAL(EL_TEST_FAIL, c.outcome);
    el_i2c_check_describe(&c, EL_BOARD_I2C_EXPECT, EL_BOARD_I2C_EXPECT_N, d, sizeof d);
    TEST_ASSERT_EQUAL_STRING("missing PCF85063(0x51), GT911(0x5D), SHT sensor(0x44)?", d);
}

static void test_i2c_unknown_device_is_listed(void)
{
    /* sensor plugged in at an address nobody told us, and GT911 strapped to its alternate */
    el_i2c_scan_t s; el_i2c_check_t c; char d[96];
    scan_full_board(&s);
    el_i2c_scan_mark(&s, 0x2A);
    el_i2c_scan_mark(&s, 0x14);
    el_i2c_check(&s, EL_BOARD_I2C_EXPECT, EL_BOARD_I2C_EXPECT_N, &c);
    TEST_ASSERT_EQUAL(EL_TEST_WARN, c.outcome);
    el_i2c_check_describe(&c, EL_BOARD_I2C_EXPECT, EL_BOARD_I2C_EXPECT_N, d, sizeof d);
    TEST_ASSERT_EQUAL_STRING("unexpected 0x14 0x2A", d);
}

static void test_i2c_describe_never_overflows(void)
{
    el_i2c_scan_t s; el_i2c_check_t c; char d[20];
    el_i2c_scan_clear(&s);
    for (uint8_t a = 0x08; a < 0x78; a++) el_i2c_scan_mark(&s, a);   /* shorted bus ACKs everything */
    el_i2c_check(&s, EL_BOARD_I2C_EXPECT, EL_BOARD_I2C_EXPECT_N, &c);
    TEST_ASSERT_EQUAL(EL_TEST_FAIL, c.outcome);
    TEST_ASSERT_TRUE(c.n_unexpected > EL_I2C_UNEXPECTED_MAX);
    memset(d, 'Z', sizeof d);
    el_i2c_check_describe(&c, EL_BOARD_I2C_EXPECT, EL_BOARD_I2C_EXPECT_N, d, sizeof d);
    TEST_ASSERT_TRUE(strlen(d) < sizeof d);
}

static void test_board_table_matches_hardware_doc(void)
{
    TEST_ASSERT_FALSE(el_board_i2c_addr_free(0x44));    /* SHT sensor, both addresses */
    TEST_ASSERT_FALSE(el_board_i2c_addr_free(0x45));
    TEST_ASSERT_FALSE(el_board_i2c_addr_free(0x51));
    TEST_ASSERT_FALSE(el_board_i2c_addr_free(0x5D));
    TEST_ASSERT_FALSE(el_board_i2c_addr_free(0x20));
    TEST_ASSERT_FALSE(el_board_i2c_addr_free(0x3F));
    TEST_ASSERT_FALSE(el_board_i2c_addr_free(0x00));
    TEST_ASSERT_FALSE(el_board_i2c_addr_free(0x78));
    TEST_ASSERT_TRUE (el_board_i2c_addr_free(0x40));    /* free for other sensors */
    TEST_ASSERT_TRUE (el_board_i2c_addr_free(0x2A));
    TEST_ASSERT_TRUE (el_board_i2c_addr_free(0x70));
}

void run_selftest_tests(void)
{
    RUN_TEST(test_runs_selected_group_in_order);
    RUN_TEST(test_failed_dependency_skips_dependents);
    RUN_TEST(test_unselected_dependency_does_not_block);
    RUN_TEST(test_test_that_does_not_report_is_a_fail);
    RUN_TEST(test_limits);
    RUN_TEST(test_bad_tables_are_rejected);
    RUN_TEST(test_summary_line);
    RUN_TEST(test_i2c_full_board_passes);
    RUN_TEST(test_i2c_logger_without_sensor_warns);
    RUN_TEST(test_i2c_missing_parts_fail_and_are_named);
    RUN_TEST(test_i2c_unknown_device_is_listed);
    RUN_TEST(test_i2c_describe_never_overflows);
    RUN_TEST(test_board_table_matches_hardware_doc);
}
