/* DIAGNOSTICS and ABOUT screens. */
#include <stdio.h>
#include <string.h>
#include "ui_screens.h"
#include "ui_port.h"
#include "storage.h"
#include "el_ui_fmt.h"
#include "el_ui_status.h"

static void vis(lv_obj_t *o, bool on)
{
    if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* ======================================================================= DIAGNOSTICS */

static struct { lv_obj_t *a[7], *b[7], *run; } DI;

static void fmt_bytes(uint64_t b, char *buf, size_t cap)
{
    if (b >= 1000000000ull) snprintf(buf, cap, "%.1f GB", (double)b / 1e9);
    else if (b >= 1000000ull) snprintf(buf, cap, "%.1f MB", (double)b / 1e6);
    else snprintf(buf, cap, "%lu kB", (unsigned long)(b / 1000));
}

static void run_cb(lv_event_t *e)
{
    (void)e;
    if (!ui_port_selftest_start()) ui_toast_locked("A self-test is already running", 2000);
}

static lv_obj_t *diag_create(void)
{
    static const char *const KA[7] = { "Sensor", "Clock", "Boot self-test", "Uptime", "Free memory", "I2C devices",
                                       "Events lost" };
    static const char *const KB[7] = { "Result log", "Internal flash", "SD card", "USB-C (PC)", "Inputs DI0 / DI1",
                                       "Output DO1", "Self-test" };
    lv_obj_t *s = ui_screen_new();
    lv_obj_t *h = ui_header(s, "Diagnostics", true);
    DI.run = ui_btn(h, UI_BTN_SECONDARY, UI_ICON_STETHOSCOPE, "Run self-test", 544, 4, 240, UI_TOUCH, run_cb, NULL);
    lv_coord_t w = (UI_W - 2 * UI_M - 12) / 2, hh = UI_H - UI_TOP - 8 - UI_M;
    lv_obj_t *l = ui_card(s, UI_M, UI_TOP + 8, w, hh);
    lv_obj_t *r = ui_card(s, UI_M + w + 12, UI_TOP + 8, w, hh);
    for (int i = 0; i < 7; i++) {
        DI.a[i] = ui_kv(l, 16, 16 + i * 46, w - 32, KA[i], "", NULL);
        DI.b[i] = ui_kv(r, 16, 16 + i * 46, w - 32, KB[i], "", NULL);
        if (i) { ui_hline(l, 16, 6 + i * 46, w - 32); ui_hline(r, 16, 6 + i * 46, w - 32); }
    }
    return s;
}

static void diag_refresh(const app_state_t *s)
{
    char buf[64], x[24], y[24];
    ui_diag_t d;
    ui_port_diag(&d);
    ui_set_text(DI.a[0], s->sensor_simulated ? "Simulated" : s->sensor_present ? "Connected" : "Not found");
    ui_set_text(DI.a[1], s->rtc_valid ? "Set" : "Not set");
    if (s->boot_selftest_fail) snprintf(buf, sizeof buf, "%u failed", s->boot_selftest_fail);
    else snprintf(buf, sizeof buf, "All passed");
    ui_set_text(DI.a[2], buf);
    lv_obj_set_style_text_color(DI.a[2], s->boot_selftest_fail ? ui_tone_text(EL_UI_TONE_BAD) : UI_PAL.text, 0);
    el_ui_fmt_uptime(s->uptime_s, buf, sizeof buf);
    ui_set_text(DI.a[3], buf);
    snprintf(buf, sizeof buf, "%lu kB + %lu MB", (unsigned long)(d.heap_free / 1024), (unsigned long)(d.psram_free / (1024 * 1024)));
    ui_set_text(DI.a[4], buf);
    ui_set_text(DI.a[5], d.i2c_text[0] ? d.i2c_text : EL_UI_NA);
    snprintf(buf, sizeof buf, "%lu", (unsigned long)(d.events_lost + d.events_dropped));
    ui_set_text(DI.a[6], buf);

    snprintf(buf, sizeof buf, "%lu results, %u%% full", (unsigned long)s->log_count, s->log_fill_pct);
    ui_set_text(DI.b[0], buf);
    lv_obj_set_style_text_color(DI.b[0], s->log_near_full ? ui_tone_text(EL_UI_TONE_WARN) : UI_PAL.text, 0);
    fmt_bytes(d.data_free, x, sizeof x);
    snprintf(buf, sizeof buf, "%s free", x);
    ui_set_text(DI.b[1], buf);
    if (d.sd_total) {
        fmt_bytes(d.sd_free, x, sizeof x);
        snprintf(buf, sizeof buf, "%s free%s", x, d.sd_mirror_errors ? ", write errors" : d.sd_mirror_behind ? ", copying" : "");
    } else snprintf(buf, sizeof buf, "No card");
    ui_set_text(DI.b[2], buf);
    ui_set_text(DI.b[3], s->usb_pc_connected ? "Serial port open" : "Not connected");
    snprintf(x, sizeof x, "%s", s->di0 ? "ON" : "off");
    snprintf(y, sizeof y, "%s", s->di1 ? "ON" : "off");
    snprintf(buf, sizeof buf, "%s / %s", x, y);
    ui_set_text(DI.b[4], buf);
    ui_set_text(DI.b[5], s->do1 ? "ON (alarm)" : "off");

    ui_selftest_status_t st;
    ui_port_selftest_status(&st);
    if (st.running) snprintf(buf, sizeof buf, "Running...");
    else if (st.done && st.fail) snprintf(buf, sizeof buf, "%u failed: %s", st.fail, st.first_fail);
    else if (st.done) snprintf(buf, sizeof buf, "%u ok, %u warn", st.pass, st.warn);
    else snprintf(buf, sizeof buf, "Not run");
    ui_set_text(DI.b[6], buf);
    lv_obj_set_style_text_color(DI.b[6], st.done && st.fail ? ui_tone_text(EL_UI_TONE_BAD) : UI_PAL.text, 0);
    ui_btn_enable(DI.run, !st.running);
}

const ui_scr_ops_t UI_SCR_DIAG = { diag_create, NULL, diag_refresh, NULL };

/* ======================================================================= ABOUT */

#define AB_ROWS 7
static struct { lv_obj_t *k[AB_ROWS], *fw_text, *install, *bar; int fw_pct; bool updating; } AB;

static void install_ok(void *u)
{
    (void)u;
    AB.updating = true;
    AB.fw_pct = 0;
    app_events_post(EVT_REQ_FW_UPDATE, SRC_UI, 0);
}
static void install_cb(lv_event_t *e)
{
    (void)e;
    if (!ui_require(EL_ACT_FW_UPDATE, "update the firmware")) return;
    ui_confirm_dialog("Install firmware?", "The logger restarts when the update is done. Keep it powered and leave the "
                      "SD card in until it restarts.", "Install", false, install_ok, NULL);
}

static lv_obj_t *about_create(void)
{
    static const char *const KEYS[AB_ROWS] = { "Serial number", "Location", "Firmware", "Board",
                                               "Temp. offset", "Humidity offset", "Sensor" };
    lv_obj_t *s = ui_screen_new();
    ui_header(s, "About", true);
    lv_obj_t *c = ui_card(s, UI_M, UI_TOP + 8, 440, UI_H - UI_TOP - 8 - UI_M);
    lv_obj_t *logo = lv_img_create(c);
    lv_img_set_src(logo, ui_logo());
    lv_img_set_pivot(logo, 0, 0);
    lv_img_set_zoom(logo, 256 * 300 / 340);          /* 340 px artwork shown 300 px wide */
    lv_obj_set_pos(logo, 20, 20);

    for (int i = 0; i < AB_ROWS; i++) {
        AB.k[i] = ui_kv(c, 20, 96 + i * 34, 400, KEYS[i], "", NULL);
        ui_hline(c, 20, 88 + i * 34, 400);
    }
    lv_obj_t *f = ui_card(s, 468, UI_TOP + 8, UI_W - 468 - UI_M, UI_H - UI_TOP - 8 - UI_M);
    ui_caption(f, 20, 14, "FIRMWARE UPDATE");
    AB.fw_text = ui_label_at(f, UI_F20, UI_PAL.text, 20, 44, "");
    lv_obj_set_width(AB.fw_text, 276);
    lv_label_set_long_mode(AB.fw_text, LV_LABEL_LONG_WRAP);
    AB.bar = lv_bar_create(f);
    lv_obj_set_pos(AB.bar, 20, 196);
    lv_obj_set_size(AB.bar, 276, 14);
    lv_bar_set_range(AB.bar, 0, 100);
    lv_obj_set_style_bg_color(AB.bar, UI_PAL.surface2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(AB.bar, UI_PAL.accent, LV_PART_INDICATOR);
    AB.install = ui_btn(f, UI_BTN_PRIMARY, LV_SYMBOL_DOWNLOAD, "Install", 20, UI_H - UI_TOP - 8 - UI_M - UI_TOUCH - 20,
                        276, UI_TOUCH, install_cb, NULL);
    return s;
}

static void about_refresh(const app_state_t *s)
{
    char buf[96], ver[24] = "";
    const el_settings_t *set = ui_settings();
    ui_set_text(AB.k[0], set->device_serial);
    ui_set_text(AB.k[1], set->location[0] ? set->location : "(not set)");
    ui_set_text(AB.k[2], ui_port_fw_version());
    ui_set_text(AB.k[3], ui_port_board_rev());
    el_ui_fmt_fixed(set->temp_offset_c, 2, UI_DEG_C, buf, sizeof buf);
    ui_set_text(AB.k[4], buf);
    el_ui_fmt_fixed(set->rh_offset_pct, 2, "%RH", buf, sizeof buf);
    ui_set_text(AB.k[5], buf);
    ui_port_sensor_info(buf, sizeof buf);
    ui_set_text(AB.k[6], buf);

    bool file = s->sd_present && ui_port_fw_file(ver, sizeof ver);
    if (AB.updating) snprintf(buf, sizeof buf, "Installing... %d%%\nDo not switch off or remove the card.", AB.fw_pct);
    else if (file) snprintf(buf, sizeof buf, "FIRMWARE.BIN found%s%s.\nRunning: %s", ver[0] ? ", version " : "", ver,
                            ui_port_fw_version());
    else if (s->sd_present) snprintf(buf, sizeof buf, "No FIRMWARE.BIN on this SD card.\nRunning: %s", ui_port_fw_version());
    else snprintf(buf, sizeof buf, "Copy FIRMWARE.BIN to a microSD card and insert it.\nRunning: %s", ui_port_fw_version());
    ui_set_text(AB.fw_text, buf);
    vis(AB.bar, AB.updating);
    lv_bar_set_value(AB.bar, AB.fw_pct, LV_ANIM_OFF);
    vis(AB.install, file && !AB.updating);
}

static void about_event(app_event_id_t id, int32_t arg, const app_state_t *s)
{
    (void)s;
    if (id == EVT_FW_UPDATE_PROGRESS) { AB.updating = true; AB.fw_pct = (int)arg; }
    if (id == EVT_FW_UPDATE_DONE) {
        AB.updating = false;
        ui_toast_locked(arg == 0 ? "Update installed - restarting" : "Update failed - the current firmware stays", 0);
    }
}

const ui_scr_ops_t UI_SCR_ABOUT = { about_create, NULL, about_refresh, about_event };
