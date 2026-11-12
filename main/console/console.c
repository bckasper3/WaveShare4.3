/* USB serial console (docs/PROTOCOLS.md sections 1-2, docs/DIAGNOSTICS.md). Parsing, access
 * checks and error replies are el_cmd; argument parsers el_app_console; this file is the
 * command table and the glue to the services. Bytes arrive on the USB task, complete lines
 * are queued to the console task, which runs commands one at a time. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "console.h"
#include "usb.h"
#include "board.h"
#include "i2c_bus.h"
#include "rtc.h"
#include "sensor.h"
#include "storage.h"
#include "app_state.h"
#include "app_events.h"
#include "app_selftest.h"
#include "app_boot.h"
#include "app_config.h"
#include "el_cmd.h"
#include "el_app_console.h"
#include "el_app_selftest.h"
#include "el_board.h"
#include "el_csv.h"
#include "el_meas.h"
#include "el_modbus.h"
#include "el_time.h"
#include "el_ui_fmt.h"

static const char *TAG = "console";

typedef struct { char text[EL_CMD_LINE_MAX]; bool overflow; } line_msg_t;

static QueueHandle_t s_lines;
static el_line_t s_rx_line;                 /* USB task only */
static volatile bool s_rx_activity;         /* any byte received (stops WATCH) */
static el_session_t s_sess;
static bool s_stream;                       /* STREAM ON (session), besides the setting */
static volatile bool s_meas_reply;          /* a MEAS from this port waits for its result */

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ---------------- output ---------------- */

static void out_fn(void *user, const char *text) { (void)user; usb_cdc_write(text, strlen(text)); }

static void csv_ctx(el_csv_ctx_t *c, el_settings_t *set)
{
    storage_settings_get(set);
    c->device_serial = set->device_serial;
    c->fw_version = FW_VERSION_STR;
    c->decimals = set->decimals;
}

static void write_csv(const el_result_t *r)
{
    static char row[EL_CSV_ROW_MAX + 2];
    el_settings_t set;
    el_csv_ctx_t c;
    csv_ctx(&c, &set);
    if (el_csv_format_result(row, EL_CSV_ROW_MAX, r, &c) != EL_OK) return;
    strcat(row, "\n");
    usb_cdc_write(row, strlen(row));
}

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 2) n = sizeof buf - 2;
    buf[n++] = '\n';
    usb_cdc_write(buf, (size_t)n);
}

/* Controller task: stream results, answer a pending MEAS. Keep short. */
static void on_event(const app_event_t *e, void *ctx)
{
    (void)ctx;
    if (e->id == EVT_MEAS_REJECTED && e->src == SRC_AUTO) return;   /* not the answer to a MEAS */
    if (e->id == EVT_MEAS_DONE) {
        el_settings_t set;
        storage_settings_get(&set);
        if (s_meas_reply || s_stream || set.serial_stream) {
            app_state_t *st = malloc(sizeof *st);
            if (st) { app_state_get(st); write_csv(&st->last_result); free(st); }
        }
        s_meas_reply = false;
    } else if (e->id == EVT_MEAS_FAULT && s_meas_reply) {
        say("ERR 7 measurement fault %s", el_meas_fault_name((el_meas_fault_t)e->arg));
        s_meas_reply = false;
    } else if (e->id == EVT_MEAS_REJECTED && s_meas_reply) {
        say("ERR 5 %s", el_meas_reject_name((el_meas_reject_t)e->arg));
        s_meas_reply = false;
    } else if (e->id == EVT_MEAS_CANCELLED && s_meas_reply) {
        say("ERR 7 cancelled");
        s_meas_reply = false;
    }
}

/* ---------------- operator commands ---------------- */

static el_cmd_err_t c_idn(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    el_settings_t s;
    storage_settings_get(&s);
    el_cmd_reply(c, "%s,%s,%s,%s", EL_MB_VENDOR, EL_MB_PRODUCT_CODE, s.device_serial, FW_VERSION_STR);
    return EL_CMD_OK;
}

static el_cmd_err_t c_meas(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    s_meas_reply = true;
    if (!app_events_post(EVT_REQ_MEASURE, SRC_SERIAL, 0)) {
        s_meas_reply = false;
        return el_cmd_error(c, EL_CMD_ERR_BUSY, "event queue full");
    }
    el_cmd_reply(c, "OK");
    return EL_CMD_OK;
}

static el_cmd_err_t c_cancel(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    app_events_post(EVT_REQ_CANCEL, SRC_SERIAL, 0);
    el_cmd_reply(c, "OK");
    return EL_CMD_OK;
}

static el_cmd_err_t c_last(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    static app_state_t st;
    app_state_get(&st);
    if (!st.has_result) return el_cmd_error(c, EL_CMD_ERR_NO_DATA, "no result yet");
    write_csv(&st.last_result);
    return EL_CMD_OK;
}

static el_cmd_err_t c_stream(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc;
    bool on;
    if (!el_app_parse_onoff(argv[0], &on)) return el_cmd_error(c, EL_CMD_ERR_ARGS, "STREAM ON|OFF");
    s_stream = on;
    el_cmd_reply(c, "OK");
    return EL_CMD_OK;
}

static bool log_cb(uint32_t index, const el_result_t *r, void *ctx)
{
    (void)index; (void)ctx;
    write_csv(r);
    return true;
}

static el_cmd_err_t c_log(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc;
    uint32_t n;
    if (!el_app_parse_u32(argv[0], 1, EL_APP_LOG_MAX_LINES, &n)) return el_cmd_error(c, EL_CMD_ERR_ARGS, "LOG? <1..%u>", EL_APP_LOG_MAX_LINES);
    uint32_t count;
    uint8_t fill;
    bool near, full;
    storage_log_status(&count, &fill, &near, &full);
    if (n > count) n = count;
    el_cmd_reply(c, "%s", el_csv_header());
    /* Oldest of the last n first, so a PC can append in order. */
    if (n && storage_log_iterate(count - n, n, EL_DATALOG_OLDEST_FIRST, log_cb, NULL) != EL_OK)
        return el_cmd_error(c, EL_CMD_ERR_FAILED, "log read failed");
    el_cmd_reply(c, "END");
    return EL_CMD_OK;
}

static el_cmd_err_t c_log_count(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    uint32_t count;
    uint8_t fill;
    bool near, full;
    storage_log_status(&count, &fill, &near, &full);
    el_cmd_reply(c, "%lu", (unsigned long)count);
    return EL_CMD_OK;
}

static el_cmd_err_t c_time_q(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    char iso[24];
    el_time_format_iso8601((int64_t)time(NULL), iso, sizeof iso);
    el_cmd_reply(c, "%s%s", iso, rtc_time_valid() ? "" : " INVALID");
    return EL_CMD_OK;
}

static el_cmd_err_t c_time_set(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc;
    if (!el_access_allows(c->level, EL_ACT_SET_TIME)) return el_cmd_error(c, EL_CMD_ERR_DENIED, "needs ADMIN");
    int64_t t;
    if (el_time_parse_iso8601(argv[0], &t) != EL_OK) return el_cmd_error(c, EL_CMD_ERR_ARGS, "TIME 2026-10-30T12:00:00Z");
    if (rtc_set((time_t)t) != ESP_OK) return el_cmd_error(c, EL_CMD_ERR_FAILED, "RTC write failed (2000-2099 only)");
    app_state_lock();
    app_state_raw()->rtc_valid = true;
    app_state_unlock();
    app_events_post(EVT_TIME_SET, SRC_SERIAL, 0);
    storage_event_log("clock set over USB");
    el_cmd_reply(c, "OK");
    return EL_CMD_OK;
}

static el_cmd_err_t c_auto(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    el_settings_t s;
    storage_settings_get(&s);
    static app_state_t st;
    app_state_get(&st);
    if (!s.log_interval_s) { el_cmd_reply(c, "interval=0 OFF"); return EL_CMD_OK; }
    el_cmd_reply(c, "interval=%lu next_in=%lu", (unsigned long)s.log_interval_s,
                 (unsigned long)(st.next_auto_s == UINT32_MAX ? 0 : st.next_auto_s));
    return EL_CMD_OK;
}

static el_cmd_err_t c_cal(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    el_settings_t s;
    storage_settings_get(&s);
    el_cmd_reply(c, "temp_offset_c=%g rh_offset_pct=%g", (double)s.temp_offset_c, (double)s.rh_offset_pct);
    return EL_CMD_OK;
}

static el_cmd_err_t c_login(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc;
    uint32_t retry = 0;
    el_login_t r;
    el_level_t want;
    if (el_app_word_eq(argv[0], "ADMIN")) {
        want = EL_LEVEL_ADMIN;
        el_settings_t s;
        storage_settings_get(&s);
        r = el_session_login(&s_sess, want, argv[1], s.admin_pin, now_ms(), &retry);
    } else if (el_app_word_eq(argv[0], "ENGINEER")) {
        want = EL_LEVEL_ENGINEER;
        el_settings_t s;
        storage_settings_get(&s);
        r = el_session_login(&s_sess, want, argv[1], s.engineer_code, now_ms(), &retry);
    } else {
        return el_cmd_error(c, EL_CMD_ERR_ARGS, "LOGIN ADMIN|ENGINEER <code>");
    }
    switch (r) {
    case EL_LOGIN_OK:
        storage_event_log("USB login %s", el_level_name(want));
        el_cmd_reply(c, "OK %s", el_level_name(want));
        return EL_CMD_OK;
    case EL_LOGIN_LOCKED:
        return el_cmd_error(c, EL_CMD_ERR_LOCKED, "retry in %lu s", (unsigned long)((retry + 999) / 1000));
    case EL_LOGIN_DISABLED:
        return el_cmd_error(c, EL_CMD_ERR_DENIED, "engineer login disabled in this build");
    default:
        if (retry) return el_cmd_error(c, EL_CMD_ERR_LOCKED, "retry in %lu s", (unsigned long)((retry + 999) / 1000));
        return el_cmd_error(c, EL_CMD_ERR_DENIED, "wrong code");
    }
}

static el_cmd_err_t c_logout(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    el_session_logout(&s_sess);
    el_cmd_reply(c, "OK OPERATOR");
    return EL_CMD_OK;
}

static el_cmd_err_t c_whoami(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    el_cmd_reply(c, "%s", el_level_name(c->level));
    return EL_CMD_OK;
}

static void test_sink(void *user, const el_test_def_t *t, const el_test_report_t *r)
{
    char line[192];
    if (el_selftest_format(line, sizeof line - 1, t, r) != EL_OK && !line[0]) return;
    el_cmd_reply((el_cmd_ctx_t *)user, "%s", line);
}

static el_cmd_err_t c_selftest(el_cmd_ctx_t *c, int argc, char **argv)
{
    el_test_summary_t sum;
    el_err_t e;
    if (argc && el_app_word_eq(argv[0], "LIST")) {
        size_t n;
        const el_test_def_t *t = app_selftest_table(&n);
        char line[160];
        for (size_t i = 0; i < n; i++) {
            el_app_selftest_format_def(line, sizeof line, &t[i]);
            el_cmd_reply(c, "%s", line);
        }
        el_cmd_reply(c, "END");
        return EL_CMD_OK;
    }
    uint32_t mask = argc ? el_app_selftest_group_mask(argv[0]) : el_app_selftest_group_mask("BOOT");
    if (mask) e = app_selftest_run_group(mask, test_sink, c, &sum);
    else e = app_selftest_run_id(argv[0], test_sink, c, &sum);
    if (e == EL_ERR_ARG) return el_cmd_error(c, EL_CMD_ERR_ARGS, "unknown test or group (SELFTEST LIST)");
    char line[96];
    el_selftest_format_summary(line, sizeof line, &sum);
    el_cmd_reply(c, "%s", line);
    return EL_CMD_OK;
}

static void print_io(el_cmd_ctx_t *c)
{
    board_io_t io;
    board_get_io(&io);
    el_cmd_reply(c, "DO0=%d DO1=%d DI0=%d DI1=%d BL=%d LCD_RST=%d TP_RST=%d SD_CS=%d",
                 io.do0_on, io.do1_on, io.di0, io.di1, io.backlight, io.lcd_rst, io.tp_rst, io.sd_cs);
}

static el_cmd_err_t c_io(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    print_io(c);
    return EL_CMD_OK;
}

static el_cmd_err_t c_status(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    static app_state_t s;
    app_state_get(&s);
    el_cmd_reply(c, "meas=%s source=%s progress=%u repeat=%u/%u fault=%s result=%d unsaved=%d results=%lu",
                 el_meas_state_name(s.meas_state), el_meas_src_name(s.meas_source), s.meas_progress_pct,
                 s.meas_repeat, s.meas_repeats, el_meas_fault_name(s.last_fault), s.has_result,
                 s.result_unsaved, (unsigned long)s.result_counter);
    el_cmd_reply(c, "next_auto=%ld log=%lu fill=%u%% near_full=%d full=%d export=%d sd=%d",
                 s.next_auto_s == UINT32_MAX ? -1L : (long)s.next_auto_s, (unsigned long)s.log_count,
                 s.log_fill_pct, s.log_near_full, s.log_full, s.export_busy, s.sd_present);
    el_cmd_reply(c, "pc=%d di0=%d di1=%d do1=%d wifi=%d", s.usb_pc_connected, s.di0, s.di1, s.do1, s.net_state);
    el_cmd_reply(c, "sensor=%s rtc_valid=%d boot_fail=%u uptime=%lu fw=%s",
                 s.sensor_simulated ? "SIM" : (s.sensor_present ? "OK" : "NONE"), s.rtc_valid,
                 s.boot_selftest_fail, (unsigned long)s.uptime_s, FW_VERSION_STR);
    return EL_CMD_OK;
}

static el_cmd_err_t c_watch(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc;
    uint32_t ms;
    if (!el_app_parse_u32(argv[0], EL_APP_WATCH_MIN_MS, EL_APP_WATCH_MAX_MS, &ms))
        return el_cmd_error(c, EL_CMD_ERR_ARGS, "WATCH <%u..%u ms>", EL_APP_WATCH_MIN_MS, EL_APP_WATCH_MAX_MS);
    s_rx_activity = false;
    while (!s_rx_activity && usb_cdc_connected()) {
        print_io(c);
        for (uint32_t t = 0; t < ms && !s_rx_activity; t += 20) vTaskDelay(pdMS_TO_TICKS(20));
    }
    xQueueReset(s_lines);          /* the key that stopped WATCH is not a command */
    el_cmd_reply(c, "END");
    return EL_CMD_OK;
}

static el_cmd_err_t c_set_q(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    el_settings_t s;
    storage_settings_get(&s);
    char line[EL_CMD_LINE_MAX + 64];
    for (int i = 0; i < EL_SET_COUNT; i++) {
        el_settings_describe_value(&s, (el_set_id_t)i, line, sizeof line);
        el_cmd_reply(c, "%s", line);
    }
    el_cmd_reply(c, "END");
    return EL_CMD_OK;
}

static el_cmd_err_t c_set(el_cmd_ctx_t *c, int argc, char **argv)
{
    el_set_id_t id = el_settings_find(argv[0]);
    if (id >= EL_SET_COUNT) return el_cmd_error(c, EL_CMD_ERR_ARGS, "unknown setting (SET?)");
    el_err_t e = storage_settings_set(id, argc > 1 ? argv[1] : "", c->level);
    switch (e) {
    case EL_OK: el_cmd_reply(c, "OK"); return EL_CMD_OK;
    case EL_ERR_DENIED: return el_cmd_error(c, EL_CMD_ERR_DENIED, "needs %s", el_level_name(el_settings_def(id)->write_level));
    case EL_ERR_IO: return el_cmd_error(c, EL_CMD_ERR_FAILED, "not saved (NVS)");
    default: {
        char d[EL_CMD_LINE_MAX];
        el_settings_describe(id, d, sizeof d);
        return el_cmd_error(c, EL_CMD_ERR_ARGS, "%s", d);
    }
    }
}

static el_cmd_err_t c_i2c_scan(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (i2c_bus_probe(a) != ESP_OK) continue;
        const char *name = "?";
        for (size_t i = 0; i < EL_BOARD_I2C_EXPECT_N; i++)
            if (a >= EL_BOARD_I2C_EXPECT[i].first && a <= EL_BOARD_I2C_EXPECT[i].last) name = EL_BOARD_I2C_EXPECT[i].name;
        el_cmd_reply(c, "I2C 0x%02X %s", a, name);
    }
    el_test_summary_t sum;
    app_selftest_run_id("I2C_BUS", test_sink, c, &sum);
    return EL_CMD_OK;
}

/* ---------------- engineer commands ---------------- */

static el_cmd_err_t c_io_set(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc;
    bool v;
    if (!el_app_parse_onoff(argv[1], &v)) return el_cmd_error(c, EL_CMD_ERR_ARGS, "IO SET <DO0|DO1|BL|LCD_RST|TP_RST> <0|1>");
    esp_err_t e = board_io_set_raw(argv[0], v);
    if (e == ESP_ERR_INVALID_ARG) return el_cmd_error(c, EL_CMD_ERR_ARGS, "DO0|DO1|BL|LCD_RST|TP_RST only");
    if (e != ESP_OK) return el_cmd_error(c, EL_CMD_ERR_FAILED, "%s", esp_err_to_name(e));
    storage_event_log("IO SET %s %d (engineer)", argv[0], v);
    print_io(c);
    return EL_CMD_OK;
}

static el_cmd_err_t c_i2c_read(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc;
    uint8_t addr, reg;
    uint32_t n;
    if (!el_app_parse_hex_u8(argv[0], &addr) || !el_app_parse_hex_u8(argv[1], &reg) || !el_app_parse_u32(argv[2], 1, 32, &n))
        return el_cmd_error(c, EL_CMD_ERR_ARGS, "I2C READ <addr> <reg> <1..32>");
    uint8_t buf[32];
    esp_err_t e = i2c_bus_raw_read(addr, reg, buf, n);
    if (e != ESP_OK) return el_cmd_error(c, EL_CMD_ERR_FAILED, "%s", esp_err_to_name(e));
    char line[3 * 32 + 1];
    for (uint32_t i = 0; i < n; i++) sprintf(&line[i * 3], "%02X ", buf[i]);
    line[n * 3 - 1] = '\0';
    el_cmd_reply(c, "%s", line);
    return EL_CMD_OK;
}

static el_cmd_err_t c_i2c_write(el_cmd_ctx_t *c, int argc, char **argv)
{
    uint8_t addr, reg, data[8];
    if (!el_app_parse_hex_u8(argv[0], &addr) || !el_app_parse_hex_u8(argv[1], &reg))
        return el_cmd_error(c, EL_CMD_ERR_ARGS, "I2C WRITE <addr> <reg> <bytes..>");
    int n = argc - 2;
    for (int i = 0; i < n; i++)
        if (!el_app_parse_hex_u8(argv[2 + i], &data[i])) return el_cmd_error(c, EL_CMD_ERR_ARGS, "bytes in hex");
    esp_err_t e = i2c_bus_raw_write(addr, reg, data, (size_t)n);
    if (e != ESP_OK) return el_cmd_error(c, EL_CMD_ERR_FAILED, "%s", esp_err_to_name(e));
    storage_event_log("I2C WRITE 0x%02X reg 0x%02X (%d bytes, engineer)", addr, reg, n);
    el_cmd_reply(c, "OK");
    return EL_CMD_OK;
}

static el_cmd_err_t c_rtc(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    time_t t;
    if (rtc_get(&t) != ESP_OK) return el_cmd_error(c, EL_CMD_ERR_FAILED, "RTC not answering / bad date");
    char iso[24];
    el_time_format_iso8601((int64_t)t, iso, sizeof iso);
    el_cmd_reply(c, "rtc=%s valid=%d system=%lld", iso, rtc_time_valid(), (long long)time(NULL));
    return EL_CMD_OK;
}

static el_cmd_err_t c_sensor_raw(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    char info[96];
    sensor_get_info(info, sizeof info);
    el_cmd_reply(c, "%s present=%d simulated=%d", info, sensor_present(), sensor_simulated());
    return EL_CMD_OK;
}

static el_cmd_err_t c_sim(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc;
    bool on;
    if (!el_app_parse_onoff(argv[0], &on)) return el_cmd_error(c, EL_CMD_ERR_ARGS, "SIM ON|OFF");
    if (sensor_set_simulation(on) != ESP_OK) return el_cmd_error(c, EL_CMD_ERR_NOT_SUPPORTED, "no SHT sensor answered at boot");
    el_cmd_reply(c, "OK");
    return EL_CMD_OK;
}

static el_cmd_err_t c_boot(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc;
    el_app_boot_mode_t m;
    if (el_app_word_eq(argv[0], "DIAG")) m = EL_APP_BOOT_DIAG;
    else if (el_app_word_eq(argv[0], "NORMAL")) m = EL_APP_BOOT_NORMAL;
    else return el_cmd_error(c, EL_CMD_ERR_ARGS, "BOOT DIAG|NORMAL");
    el_cmd_reply(c, "OK rebooting %s", el_app_boot_mode_name(m));
    storage_event_log("reboot to %s (engineer)", el_app_boot_mode_name(m));
    storage_event_flush(500);
    vTaskDelay(pdMS_TO_TICKS(100));
    app_boot_request(m);
    return EL_CMD_OK;
}

static el_cmd_err_t c_bootloader(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    el_cmd_reply(c, "OK rebooting to download mode");
    storage_event_flush(500);
    vTaskDelay(pdMS_TO_TICKS(100));
    usb_reboot_to_bootloader();
    return EL_CMD_OK;
}

static el_cmd_err_t c_help(el_cmd_ctx_t *c, int argc, char **argv);

#define OP EL_LEVEL_OPERATOR
#define AD EL_LEVEL_ADMIN
#define EN EL_LEVEL_ENGINEER
static const el_cmd_def_t TABLE[] = {
    { "*IDN?",         OP, 0, 0, c_idn,        "",                 "identity" },
    { "MEAS",          OP, 0, 0, c_meas,       "",                 "start a measurement; the result line follows" },
    { "CANCEL",        OP, 0, 0, c_cancel,     "",                 "cancel the running measurement" },
    { "LAST?",         OP, 0, 0, c_last,       "",                 "last result (CSV)" },
    { "STREAM",        OP, 1, 1, c_stream,     "ON|OFF",           "push every new result (CSV)" },
    { "LOG?",          OP, 1, 1, c_log,        "<n>",              "last n results (CSV), then END" },
    { "LOG COUNT?",    OP, 0, 0, c_log_count,  "",                 "number of stored results" },
    { "AUTO?",         OP, 0, 0, c_auto,       "",                 "logging interval and seconds to the next reading" },
    { "TIME?",         OP, 0, 0, c_time_q,     "",                 "clock (UTC)" },
    { "TIME",          OP, 1, 1, c_time_set,   "<ISO-8601>",       "set the clock (ADMIN)" },
    { "CAL?",          OP, 0, 0, c_cal,        "",                 "calibration offsets (change with SET)" },
    { "HELP",          OP, 0, 0, c_help,       "",                 "this list" },
    { "LOGIN",         OP, 2, 2, c_login,      "ADMIN|ENGINEER <code>", "raise the access level" },
    { "LOGOUT",        OP, 0, 0, c_logout,     "",                 "back to OPERATOR" },
    { "WHOAMI?",       OP, 0, 0, c_whoami,     "",                 "current level" },
    { "SELFTEST",      OP, 0, 1, c_selftest,   "[LIST|BOOT|FULL|FIXTURE|INTERACTIVE|<id>]", "run self-tests" },
    { "IO?",           OP, 0, 0, c_io,         "",                 "every digital line" },
    { "STATUS?",       OP, 0, 0, c_status,     "",                 "state snapshot" },
    { "WATCH",         OP, 1, 1, c_watch,      "<ms>",             "repeat IO? until any key" },
    { "SET?",          OP, 0, 0, c_set_q,      "",                 "list settings" },
    { "SET",           OP, 1, 2, c_set,        "<key> <value>",    "change a setting (level per setting)" },
    { "I2C SCAN",      OP, 0, 0, c_i2c_scan,   "",                 "list I2C devices" },
    { "IO SET",        EN, 2, 2, c_io_set,     "<DO0|DO1|BL|LCD_RST|TP_RST> <0|1>", "drive one line" },
    { "I2C READ",      EN, 3, 3, c_i2c_read,   "<addr> <reg> <n>", "raw register read (hex)" },
    { "I2C WRITE",     EN, 3, 10, c_i2c_write, "<addr> <reg> <bytes..>", "raw register write (hex)" },
    { "RTC?",          EN, 0, 0, c_rtc,        "",                 "RTC raw" },
    { "SENSOR RAW?",   EN, 0, 0, c_sensor_raw, "",                 "sensor driver and state" },
    { "SIM",           EN, 1, 1, c_sim,        "ON|OFF",           "simulated sensor" },
    { "BOOT",          EN, 1, 1, c_boot,       "DIAG|NORMAL",      "reboot into a boot mode" },
    { "BOOTLOADER",    EN, 0, 0, c_bootloader, "",                 "reboot into ROM download mode" },
};
#define TABLE_N (sizeof TABLE / sizeof TABLE[0])

static el_cmd_err_t c_help(el_cmd_ctx_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    el_cmd_print_help(TABLE, TABLE_N, c);
    return EL_CMD_OK;
}

/* ---------------- plumbing ---------------- */

static void rx_handler(const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;
    if (len) s_rx_activity = true;
    for (size_t i = 0; i < len; i++) {
        el_line_status_t st = el_line_feed(&s_rx_line, (char)data[i]);
        if (st == EL_LINE_PENDING) continue;
        line_msg_t m = { .overflow = (st == EL_LINE_OVERFLOW) };
        if (st == EL_LINE_READY) {
            strncpy(m.text, s_rx_line.buf, sizeof m.text - 1);
            m.text[sizeof m.text - 1] = '\0';
        }
        xQueueSend(s_lines, &m, 0);      /* full queue: line dropped (PC sends too fast) */
    }
}

static void console_task(void *arg)
{
    (void)arg;
    static line_msg_t m;
    el_cmd_ctx_t ctx = { .out = out_fn, .user = NULL };
    for (;;) {
        if (xQueueReceive(s_lines, &m, portMAX_DELAY) != pdTRUE) continue;
        uint32_t now = now_ms();
        el_settings_t set;
        storage_settings_get(&set);
        s_sess.idle_timeout_ms = (uint32_t)set.login_idle_s * 1000u;
        ctx.level = el_session_level(&s_sess, now);
        el_session_touch(&s_sess, now);
        if (m.overflow) {
            el_cmd_error(&ctx, EL_CMD_ERR_TOO_LONG, "line longer than %d bytes", EL_CMD_LINE_MAX - 1);
            continue;
        }
        el_cmd_execute(TABLE, TABLE_N, m.text, &ctx);
    }
}

esp_err_t console_init(void)
{
    if (s_lines) return ESP_OK;
    s_lines = xQueueCreate(4, sizeof(line_msg_t));
    if (!s_lines) return ESP_ERR_NO_MEM;
    el_line_init(&s_rx_line);
    el_settings_t set;
    storage_settings_get(&set);
    el_session_init(&s_sess, (uint32_t)set.login_idle_s * 1000u, now_ms());
    app_events_add_listener(on_event, NULL);
    usb_cdc_set_rx_handler(rx_handler, NULL);
    if (xTaskCreatePinnedToCore(console_task, "console", 6144, NULL, 4, NULL, 0) != pdPASS)
        return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "console ready (%u commands)", (unsigned)TABLE_N);
    return ESP_OK;
}
