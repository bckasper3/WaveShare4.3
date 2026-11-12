/* Board self-tests (docs/DIAGNOSTICS.md section 7). Every threshold and verdict comes from
 * envlogger_core (el_hw_check, el_i2c_check, el_hw_ch422g); this file only moves
 * bytes. Tests restore every output they touch. */
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "board.h"
#include "board_priv.h"
#include "display.h"
#include "i2c_bus.h"
#include "rtc.h"
#include "sd_card.h"
#include "el_board.h"
#include "el_hw_ch422g.h"
#include "el_hw_check.h"
#include "el_i2c_check.h"
#include "el_util.h"

static void t_i2c_bus(void *ctx, el_test_report_t *r)
{
    (void)ctx;
    if (!i2c_bus_lines_were_idle()) {
        el_test_set(r, EL_TEST_FAIL, NAN, "SDA/SCL stuck low at boot");
        return;
    }
    el_i2c_scan_t scan;
    el_i2c_scan_clear(&scan);
    for (uint8_t a = 0x08; a < 0x78; a++)
        if (i2c_bus_probe(a) == ESP_OK) el_i2c_scan_mark(&scan, a);
    el_i2c_check_t c;
    if (el_i2c_check(&scan, EL_BOARD_I2C_EXPECT, EL_BOARD_I2C_EXPECT_N, &c) != EL_OK) {
        el_test_set(r, EL_TEST_FAIL, NAN, "bad expect table");
        return;
    }
    r->outcome = c.outcome;
    r->value = (float)c.n_found;
    el_i2c_check_describe(&c, EL_BOARD_I2C_EXPECT, EL_BOARD_I2C_EXPECT_N, r->detail, sizeof r->detail);
}

static void t_ch422g(void *ctx, el_test_report_t *r)
{
    (void)ctx;
    if (!board_present()) { el_test_set(r, EL_TEST_FAIL, NAN, "no answer at boot"); return; }
    el_hw_ch422g_t s = { .io = board_io_shadow(), .oc = 0 };
    for (unsigned n = 0; n < 4; n++) {
        el_hw_ch422g_t t = s;
        t.io = el_hw_ch422g_test_pattern(&s, n);
        uint8_t raw = 0;
        esp_err_t e = board_io_readback(t.io, &raw);
        if (e != ESP_OK) { el_test_set(r, EL_TEST_FAIL, NAN, "I2C error pattern %u", n); return; }
        if (!el_hw_ch422g_readback_ok(&t, raw)) {
            el_test_set(r, EL_TEST_FAIL, NAN, "read-back 0x%02X, wrote 0x%02X", raw, t.io);
            return;
        }
    }
    el_test_set(r, EL_TEST_PASS, NAN, "4 patterns read back");
}

static void t_rtc(void *ctx, el_test_report_t *r)
{
    (void)ctx;
    time_t t0 = 0, t1 = 0;
    if (rtc_get(&t0) != ESP_OK) { el_test_set(r, EL_TEST_FAIL, NAN, "no answer / bad date"); return; }
    vTaskDelay(pdMS_TO_TICKS(EL_HW_RTC_TICK_WAIT_MS));
    if (rtc_get(&t1) != ESP_OK) { el_test_set(r, EL_TEST_FAIL, NAN, "second read failed"); return; }
    el_test_outcome_t o = el_hw_judge_rtc_tick((int64_t)t0, (int64_t)t1);
    if (o != EL_TEST_PASS) { el_test_set(r, o, (float)(t1 - t0), "ticked %lld s in 1.1 s", (long long)(t1 - t0)); return; }
    if (!rtc_time_valid()) { el_test_set(r, EL_TEST_WARN, (float)(t1 - t0), "running, time not set"); return; }
    el_test_set(r, EL_TEST_PASS, (float)(t1 - t0), "running, time valid");
}

static void t_touch(void *ctx, el_test_report_t *r)
{
    (void)ctx;
    uint8_t id[4] = { 0 };
    if (!display_touch_ok() || display_touch_read_id(id) != ESP_OK) {
        el_test_set(r, EL_TEST_FAIL, NAN, "GT911 not answering");
        return;
    }
    el_test_outcome_t o = el_hw_judge_gt911_id(id);
    el_test_set(r, o, NAN, "ID %02X %02X %02X %02X", id[0], id[1], id[2], id[3]);
}

static void t_psram(void *ctx, el_test_report_t *r)
{
    (void)ctx;
    size_t total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    el_test_outcome_t o = el_hw_judge_psram_size(total);
    if (o == EL_TEST_FAIL) { el_test_set(r, o, (float)(total >> 20), "%u bytes", (unsigned)total); return; }
    const size_t words = 64 * 1024 / 4;
    uint32_t *buf = heap_caps_malloc(words * 4, MALLOC_CAP_SPIRAM);
    if (!buf) { el_test_set(r, EL_TEST_FAIL, NAN, "64 kB alloc failed"); return; }
    for (unsigned p = 0; p < EL_HW_MEM_PASSES; p++) {
        size_t bad;
        el_hw_mem_fill(buf, words, p, 0x5A5AC3A5u);
        size_t n = el_hw_mem_check(buf, words, p, 0x5A5AC3A5u, &bad);
        if (n) {
            heap_caps_free(buf);
            el_test_set(r, EL_TEST_FAIL, (float)n, "pass %u: %u bad words, first %u", p, (unsigned)n, (unsigned)bad);
            return;
        }
    }
    heap_caps_free(buf);
    el_test_set(r, o, (float)(total >> 20), "%u kB, 64 kB pattern OK", (unsigned)(total >> 10));
}

static void t_flash(void *ctx, el_test_report_t *r)
{
    (void)ctx;
    uint32_t sz = 0;
    if (esp_flash_get_size(NULL, &sz) != ESP_OK) { el_test_set(r, EL_TEST_FAIL, NAN, "size read failed"); return; }
    el_test_set(r, el_hw_judge_flash_size(sz), (float)(sz >> 20), "%lu bytes", (unsigned long)sz);
}

static void t_display(void *ctx, el_test_report_t *r)
{
    (void)ctx;
    esp_err_t e = display_show_test_pattern(5000);
    if (e != ESP_OK) { el_test_set(r, EL_TEST_FAIL, NAN, "pattern: %s", esp_err_to_name(e)); return; }
    /* The operator judges the picture; the console records their answer. */
    el_test_set(r, EL_TEST_WARN, NAN, "pattern shown 5 s: operator must confirm");
}

static void t_touch_corners(void *ctx, el_test_report_t *r)
{
    (void)ctx;
    el_test_set(r, EL_TEST_SKIP, NAN, "needs the UI touch-target screen (not built yet)");
}

static void t_dio_loop(void *ctx, el_test_report_t *r)
{
    (void)ctx;
    board_io_t io;
    board_get_io(&io);
    unsigned bad = 0;
    for (unsigned i = 0; i < EL_HW_DIO_CYCLES; i++) {
        bool on = (i & 1u) != 0, di0 = false;
        board_set_do1(on);
        vTaskDelay(pdMS_TO_TICKS(EL_HW_DIO_SETTLE_MS));
        if (board_read_di(&di0, NULL) != ESP_OK || di0 != el_hw_dio_expected_di0(on)) bad++;
    }
    board_set_do1(io.do1_on);
    el_test_set(r, bad ? EL_TEST_FAIL : EL_TEST_PASS, (float)bad, "%u/%u cycles wrong", bad, EL_HW_DIO_CYCLES);
}

static void t_sd(void *ctx, el_test_report_t *r)
{
    (void)ctx;
    uint64_t fr = 0, tot = 0;
    if (!sd_card_mounted() || sd_card_space(&fr, &tot) != ESP_OK) {
        el_test_set(r, EL_TEST_WARN, NAN, "no card (optional)");
        return;
    }
    el_test_set(r, EL_TEST_PASS, (float)(fr >> 20), "%llu of %llu MB free",
                (unsigned long long)(fr >> 20), (unsigned long long)(tot >> 20));
}

static const el_test_def_t TESTS[] = {
    { "I2C_BUS",       "I2C bus and device scan",       "devices", NULL,      EL_TG_BOOT,        t_i2c_bus },
    { "CH422G",        "IO expander write/read-back",   "",        "I2C_BUS", EL_TG_BOOT,        t_ch422g },
    { "RTC",           "Real-time clock running",       "s",       "I2C_BUS", EL_TG_BOOT,        t_rtc },
    { "TOUCH",         "GT911 touch controller",        "",        "I2C_BUS", EL_TG_BOOT,        t_touch },
    { "PSRAM",         "PSRAM size and pattern",        "MB",      NULL,      EL_TG_FULL,        t_psram },
    { "FLASH_ID",      "Flash size",                    "MB",      NULL,      EL_TG_FULL,        t_flash },
    { "DISPLAY",       "Display test pattern",          "",        "CH422G",  EL_TG_INTERACTIVE, t_display },
    { "TOUCH_CORNERS", "Touch four corners and centre", "px",      "TOUCH",   EL_TG_INTERACTIVE, t_touch_corners },
    { "DIO_LOOP",      "DO1 to DI0 loopback",           "errors",  "CH422G",  EL_TG_FIXTURE,     t_dio_loop },
    { "SD",            "microSD card (optional)",       "MB",      NULL,      EL_TG_FULL,        t_sd },
};

const el_test_def_t *board_selftests(size_t *count)
{
    if (count) *count = sizeof TESTS / sizeof TESTS[0];
    return TESTS;
}
