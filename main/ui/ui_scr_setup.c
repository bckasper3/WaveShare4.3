/* CALIBRATE, SETTINGS and SET_TIME screens. */
#include <stdio.h>
#include <string.h>
#include "ui_screens.h"
#include "ui_port.h"
#include "storage.h"
#include "el_time.h"
#include "el_ui_fmt.h"
#include "el_ui_settings.h"
#include "el_ui_status.h"
#include "el_stats.h"

/* ======================================================================= CALIBRATE */
/* Offset calibration against a reference instrument (Admin): place the reference next to the
 * sensor, take a reading, enter what the reference shows. The new offset is
 *   offset = reference - (reading - current offset)
 * i.e. the raw sensor value is corrected to the reference. Offsets are ordinary settings
 * (temp_offset_c, rh_offset_pct), so they are also reachable over USB serial and Settings. */

static struct { lv_obj_t *t_raw, *h_raw, *t_off, *h_off, *when, *b_ref_t, *b_ref_h, *b_reset; } CA;

static bool cal_have(const app_state_t *s) { return s->has_result && s->last_result.status != EL_RES_ERROR; }

static bool cal_apply(el_set_id_t id, float v)
{
    char t[24];
    snprintf(t, sizeof t, "%.2f", (double)v);
    el_err_t e = storage_settings_set(id, t, ui_level());
    if (e == EL_ERR_RANGE) {
        ui_toast_locked(id == EL_SET_T_OFFSET ? "Offset would exceed +/-10 \xC2\xB0" "C - check the reference"
                                               : "Offset would exceed +/-20 %RH - check the reference", 3500);
        return false;
    }
    if (e != EL_OK) { ui_toast_locked("Could not save the offset", 3000); return false; }
    ui_toast_locked("Offset saved - it applies from the next reading", 3000);
    return true;
}

static bool ref_entered(const char *text, void *user)
{
    bool hum = user != NULL;
    float ref = 0;
    if (sscanf(text, "%f", &ref) != 1) { ui_toast_locked("Not a number", 2000); return false; }
    app_state_t s;
    app_state_get(&s);
    if (!cal_have(&s)) return true;
    const el_settings_t *set = ui_settings();
    /* The logged value already contains the offset in force: take it out first. */
    float raw = hum ? s.last_result.rh_pct - set->rh_offset_pct : s.last_result.temp_c - set->temp_offset_c;
    return cal_apply(hum ? EL_SET_RH_OFFSET : EL_SET_T_OFFSET, ref - raw);
}

static void ref_cb(lv_event_t *e)
{
    bool hum = lv_event_get_user_data(e) != NULL;
    if (!ui_require(EL_ACT_CALIBRATE, "calibrate")) return;
    app_state_t s;
    app_state_get(&s);
    if (!cal_have(&s)) { ui_toast_locked("Take a reading first", 2500); return; }
    ui_text_dialog(hum ? "Reference humidity (%RH)" : "Reference temperature (\xC2\xB0" "C)", "", EL_UI_CS_DECIMAL, 7,
                   false, ref_entered, hum ? (void *)1 : NULL);
}

static void reset_ok(void *u)
{
    (void)u;
    if (storage_settings_set(EL_SET_T_OFFSET, "0", ui_level()) == EL_OK &&
        storage_settings_set(EL_SET_RH_OFFSET, "0", ui_level()) == EL_OK)
        ui_toast_locked("Offsets reset to zero", 2500);
}
static void reset_cb(lv_event_t *e)
{
    (void)e;
    if (!ui_require(EL_ACT_CALIBRATE, "calibrate")) return;
    ui_confirm_dialog("Reset both offsets?", "Temperature and humidity offsets go back to 0.", "Reset", true,
                      reset_ok, NULL);
}
static void cal_measure_cb(lv_event_t *e) { (void)e; app_events_post(EVT_REQ_MEASURE, SRC_UI, 0); }

static lv_obj_t *cal_create(void)
{
    lv_obj_t *s = ui_screen_new();
    lv_obj_t *h = ui_header(s, "Calibration", true);
    ui_btn(h, UI_BTN_SECONDARY, UI_ICON_THERMO, "Measure now", 584, 4, 200, UI_TOUCH, cal_measure_cb, NULL);

    lv_obj_t *c = ui_card(s, UI_M, UI_TOP + 8, 380, UI_H - UI_TOP - 8 - UI_M);
    ui_caption(c, 20, 14, "SENSOR (WITHOUT OFFSET)");
    CA.t_raw = ui_kv(c, 20, 44, 340, "Temperature", "", UI_F24);
    CA.h_raw = ui_kv(c, 20, 84, 340, "Humidity", "", UI_F24);
    CA.when = ui_label_at(c, UI_F16, UI_PAL.text3, 20, 124, "");
    ui_hline(c, 20, 156, 340);
    ui_caption(c, 20, 168, "OFFSETS IN FORCE");
    CA.t_off = ui_kv(c, 20, 196, 340, "Temperature", "", UI_F24);
    CA.h_off = ui_kv(c, 20, 236, 340, "Humidity", "", UI_F24);

    lv_obj_t *a = ui_card(s, 408, UI_TOP + 8, UI_W - 408 - UI_M, UI_H - UI_TOP - 8 - UI_M);
    ui_caption(a, 20, 14, "SET FROM A REFERENCE (ADMIN)");
    lv_obj_t *hint = ui_label_at(a, UI_F16, UI_PAL.text2, 20, 38,
                                 "Put a reference instrument next to the sensor, let both settle, take a reading, "
                                 "then enter what the reference shows.");
    lv_obj_set_width(hint, 320);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    CA.b_ref_t = ui_btn(a, UI_BTN_PRIMARY, UI_ICON_THERMO, "Reference temperature", 20, 112, 320, UI_TOUCH, ref_cb, NULL);
    CA.b_ref_h = ui_btn(a, UI_BTN_PRIMARY, UI_ICON_TINT, "Reference humidity", 20, 186, 320, UI_TOUCH, ref_cb, (void *)1);
    CA.b_reset = ui_btn(a, UI_BTN_DANGER, LV_SYMBOL_REFRESH, "Reset offsets", 20, 260, 320, UI_TOUCH, reset_cb, NULL);
    return s;
}

static void cal_show(const app_state_t *s)
{
    char buf[48], v[24];
    const el_settings_t *set = ui_settings();
    if (cal_have(s)) {
        ui_fmt_temp(s->last_result.temp_c - set->temp_offset_c, v, sizeof v);
        snprintf(buf, sizeof buf, "%s " UI_DEG_C, v);
        ui_set_text(CA.t_raw, buf);
        ui_fmt_rh(s->last_result.rh_pct - set->rh_offset_pct, v, sizeof v);
        snprintf(buf, sizeof buf, "%s %%RH", v);
        ui_set_text(CA.h_raw, buf);
        el_ui_fmt_ago(ui_port_now(), s->last_result.timestamp_s, v, sizeof v);
        snprintf(buf, sizeof buf, "Reading %s", v);
        ui_set_text(CA.when, buf);
    } else {
        ui_set_text(CA.t_raw, EL_UI_NA);
        ui_set_text(CA.h_raw, EL_UI_NA);
        ui_set_text(CA.when, "No reading yet - press Measure now");
    }
    el_ui_fmt_fixed(set->temp_offset_c, 2, UI_DEG_C, buf, sizeof buf);
    ui_set_text(CA.t_off, buf);
    el_ui_fmt_fixed(set->rh_offset_pct, 2, "%RH", buf, sizeof buf);
    ui_set_text(CA.h_off, buf);
    bool ok = cal_have(s);
    ui_btn_enable(CA.b_ref_t, ok);
    ui_btn_enable(CA.b_ref_h, ok);
}

static void cal_event(app_event_id_t id, int32_t arg, const app_state_t *s)
{
    (void)arg;
    if (id == EVT_SETTINGS_CHANGED || id == EVT_MEAS_DONE || id == EVT_LOG_CHANGED) cal_show(s);
}

const ui_scr_ops_t UI_SCR_CALIBRATE = { cal_create, cal_show, NULL, cal_event };

/* ======================================================================= SETTINGS */

#define SG_SYSTEM EL_UI_SG_COUNT          /* extra page: links to the system screens */
#define SET_ROWS  16

static struct {
    lv_obj_t *grp[EL_UI_SG_COUNT + 1], *list, *login;
    int       group;
} SE;

static const char *set_err_text(el_err_t e)
{
    switch (e) {
    case EL_ERR_DENIED: return "Log in as Admin to change this";
    case EL_ERR_RANGE:  return "Value out of range";
    case EL_ERR_ARG:    return "Not a valid value";
    case EL_ERR_IO:     return "Saved for now, but could not be written to flash";
    default:            return "Could not change the setting";
    }
}

static void se_render(void);

static bool apply(el_set_id_t id, const char *text)
{
    el_err_t e = storage_settings_set(id, text, ui_level());
    if (e != EL_OK) {
        char buf[96];
        if (e == EL_ERR_RANGE) {
            const el_set_def_t *d = el_settings_def(id);
            if (d && (d->type == EL_SET_T_INT || d->type == EL_SET_T_FLOAT))
                snprintf(buf, sizeof buf, "Allowed: %g to %g %s", (double)d->min, (double)d->max, d->unit);
            else snprintf(buf, sizeof buf, "%s", set_err_text(e));
        } else snprintf(buf, sizeof buf, "%s", set_err_text(e));
        ui_toast_locked(buf, 3000);
        return e == EL_ERR_IO;
    }
    se_render();
    return true;
}

static bool text_set(const char *text, void *user) { return apply((el_set_id_t)(intptr_t)user, text); }

static el_set_id_t s_choice_id;
static void choice_set(int idx, void *user)
{
    (void)user;
    const el_set_def_t *d = el_settings_def(s_choice_id);
    if (d && idx >= 0 && idx < d->enum_count) apply(s_choice_id, d->enum_names[idx]);
}

static void row_cb(lv_event_t *e)
{
    el_set_id_t id = (el_set_id_t)(intptr_t)lv_event_get_user_data(e);
    const el_set_def_t *d = el_settings_def(id);
    if (!d) return;
    if (!el_ui_set_writable(id, ui_level())) {
        char what[64];
        snprintf(what, sizeof what, "change %s", el_ui_set_title(id));
        if (d->write_level == EL_LEVEL_ENGINEER) ui_login_dialog(EL_LEVEL_ENGINEER);
        else ui_require(EL_ACT_EDIT_SETTINGS, what);
        return;
    }
    el_settings_t s;
    storage_settings_get(&s);
    char cur[EL_UPLINK_URL_MAX + 8] = "";
    el_settings_get_str(&s, id, cur, sizeof cur);
    switch (el_ui_set_editor(id)) {
    case EL_UI_ED_TOGGLE:
        apply(id, strcmp(cur, "1") == 0 ? "0" : "1");
        break;
    case EL_UI_ED_CHOICE: {
        static const char *opts[16];
        int n = d->enum_count < 16 ? d->enum_count : 16;
        for (int i = 0; i < n; i++) opts[i] = el_ui_set_enum_text(id, (uint8_t)i);
        int sel = -1;
        for (int i = 0; i < n; i++) if (strcmp(cur, d->enum_names[i]) == 0) sel = i;
        s_choice_id = id;
        ui_choice_dialog(el_ui_set_title(id), opts, n, sel, choice_set, NULL);
        break;
    }
    case EL_UI_ED_INT:
        ui_text_dialog(el_ui_set_title(id), cur, d->min < 0 ? EL_UI_CS_INT : EL_UI_CS_DIGITS, 7, false, text_set,
                       (void *)(intptr_t)id);
        break;
    case EL_UI_ED_FLOAT:
        ui_text_dialog(el_ui_set_title(id), cur, EL_UI_CS_DECIMAL, 9, false, text_set, (void *)(intptr_t)id);
        break;
    case EL_UI_ED_PIN: {
        char title[64];
        snprintf(title, sizeof title, d->min > 0 ? "New %s" : "New %s (empty = none)", el_ui_set_title(id));
        ui_text_dialog(title, "", EL_UI_CS_DIGITS, (size_t)d->max, true, text_set, (void *)(intptr_t)id);
        break;
    }
    case EL_UI_ED_SECRET: {
        char title[64];
        snprintf(title, sizeof title, "%s (empty = none)", el_ui_set_title(id));
        ui_text_dialog(title, "", EL_UI_CS_TEXT, (size_t)d->max, true, text_set, (void *)(intptr_t)id);
        break;
    }
    case EL_UI_ED_TEXT:
        ui_text_dialog(el_ui_set_title(id), cur, d->digits_only ? EL_UI_CS_DIGITS : EL_UI_CS_TEXT, (size_t)d->max,
                       false, text_set, (void *)(intptr_t)id);
        break;
    }
}

static void sys_cb(lv_event_t *e) { ui_go((ui_screen_t)(intptr_t)lv_event_get_user_data(e)); }
static void screen_off_cb(lv_event_t *e) { (void)e; ui_screen_off(); }

static lv_obj_t *add_row(lv_coord_t y, const char *title, const char *value, bool locked, lv_event_cb_t cb, void *user)
{
    lv_obj_t *r = ui_row_btn(SE.list, 0, y, 504, 56, cb, user);
    lv_obj_t *t = ui_label_at(r, UI_F20, UI_PAL.text, 14, 16, title);
    lv_obj_set_width(t, 250);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_t *v = ui_label(r, UI_F20, locked ? UI_PAL.text3 : UI_PAL.accent, value);
    lv_obj_set_width(v, 200);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, -40, 0);
    if (locked) {
        lv_obj_t *lk = lv_obj_get_child(r, 0);          /* the chevron becomes a lock */
        lv_label_set_text(lk, UI_ICON_LOCK);
        lv_obj_set_style_text_font(lk, UI_F16, 0);
    }
    if (y) ui_hline(SE.list, 14, y, 476);
    return r;
}

static void se_render(void)
{
    for (int g = 0; g <= EL_UI_SG_COUNT; g++) {
        bool sel = g == SE.group;
        lv_obj_set_style_bg_color(SE.grp[g], sel ? UI_PAL.accent : UI_PAL.surface, 0);
        lv_obj_set_style_bg_opa(SE.grp[g], LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(SE.grp[g], sel ? UI_PAL.on_accent : UI_PAL.text, 0);
    }
    el_level_t lv = ui_level();
    ui_btn_set_text(SE.login, lv > EL_LEVEL_OPERATOR ? UI_ICON_SIGN_OUT : UI_ICON_SIGN_IN,
                    lv > EL_LEVEL_OPERATOR ? "Log out" : "Log in");
    lv_obj_clean(SE.list);
    lv_obj_scroll_to_y(SE.list, 0, LV_ANIM_OFF);
    if (SE.group == SG_SYSTEM) {
        static const struct { const char *t, *v; ui_screen_t s; } SYS[] = {
            { "Date & time", "Set", SCR_SET_TIME }, { "Calibration", "Open", SCR_CALIBRATE },
            { "Diagnostics", "Open", SCR_DIAG },    { "About & firmware", "Open", SCR_ABOUT },
            { "Wi-Fi", "Open", SCR_WIFI },
        };
        for (size_t i = 0; i < sizeof SYS / sizeof SYS[0]; i++)
            add_row((lv_coord_t)(i * 56), SYS[i].t, SYS[i].v, false, sys_cb, (void *)(intptr_t)SYS[i].s);
        return;
    }
    el_settings_t s;
    storage_settings_get(&s);
    size_t n = el_ui_set_group_count((el_ui_set_group_t)SE.group);
    lv_coord_t y = 0;
    if (SE.group == EL_UI_SG_DISPLAY) {          /* backlight: on/off on this board, so an action, not a level */
        add_row(y, "Turn the screen off now", UI_ICON_MOON, false, screen_off_cb, NULL);
        y += 56;
    }
    if (SE.group == EL_UI_SG_COMMS) {            /* Wi-Fi has its own screen: status, scan, test */
        add_row(y, "Wi-Fi status and setup", "Open", false, sys_cb, (void *)(intptr_t)SCR_WIFI);
        y += 56;
    }
    for (size_t i = 0; i < n && i < SET_ROWS; i++) {
        el_set_id_t id = el_ui_set_group_item((el_ui_set_group_t)SE.group, i);
        if (!ui_port_backlight_dimmable() && (id == EL_SET_BRIGHTNESS || id == EL_SET_IDLE_DIM)) continue;
        char v[40];
        el_ui_set_value_text(&s, id, v, sizeof v);
        add_row(y, el_ui_set_title(id), v, !el_ui_set_writable(id, lv), row_cb, (void *)(intptr_t)id);
        y += 56;
    }
}

static void grp_cb(lv_event_t *e) { SE.group = (int)(intptr_t)lv_event_get_user_data(e); se_render(); }
static void login_cb(lv_event_t *e)
{
    (void)e;
    if (ui_level() > EL_LEVEL_OPERATOR) ui_logout();
    else ui_login_dialog(EL_LEVEL_OPERATOR);
}

static lv_obj_t *se_create(void)
{
    lv_obj_t *s = ui_screen_new();
    lv_obj_t *h = ui_header(s, "Settings", true);
    SE.login = ui_btn(h, UI_BTN_SECONDARY, UI_ICON_SIGN_IN, "Log in", 604, 4, 180, UI_TOUCH, login_cb, NULL);
    lv_obj_t *gc = ui_box(s, UI_M, UI_TOP + 8, 220, UI_H - UI_TOP - 8 - UI_M);
    for (int g = 0; g <= EL_UI_SG_COUNT; g++) {
        const char *name = g == SG_SYSTEM ? "System" : el_ui_set_group_name((el_ui_set_group_t)g);
        SE.grp[g] = ui_btn(gc, UI_BTN_GHOST, NULL, name, 0, g * 49, 220, 45, grp_cb, (void *)(intptr_t)g);
        lv_obj_t *l = lv_obj_get_child(SE.grp[g], 0);
        lv_obj_set_style_text_font(l, UI_F20, 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 16, 0);
    }
    lv_obj_t *lc = ui_card(s, 248, UI_TOP + 8, UI_W - 248 - UI_M, UI_H - UI_TOP - 8 - UI_M);
    SE.list = ui_box(lc, 8, 4, 520, UI_H - UI_TOP - 8 - UI_M - 8);
    lv_obj_add_flag(SE.list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(SE.list, LV_DIR_VER);
    return s;
}

static void se_show(const app_state_t *s) { (void)s; se_render(); }
static void se_event(app_event_id_t id, int32_t arg, const app_state_t *s)
{
    (void)arg; (void)s;
    if (id == EVT_SETTINGS_CHANGED) se_render();
}

const ui_scr_ops_t UI_SCR_SETTINGS = { se_create, se_show, NULL, se_event };

void ui_settings_open(int group)
{
    SE.group = group < 0 || group > EL_UI_SG_COUNT ? 0 : group;
    if (ui_current() == SCR_SETTINGS) se_render();
    else ui_go(SCR_SETTINGS);
}

/* ======================================================================= SET TIME */

static struct { lv_obj_t *r[5], *now, *tz; } ST;
#define YEAR0 2024

static void roller_style(lv_obj_t *r, lv_coord_t w)
{
    lv_obj_set_style_text_font(r, UI_F32, 0);          /* font first: row count sizes from it */
    lv_obj_set_style_text_line_space(r, 18, 0);
    lv_obj_set_width(r, w);
    lv_roller_set_visible_row_count(r, 3);
    lv_obj_set_style_bg_color(r, UI_PAL.accent_tint, LV_PART_SELECTED);
    lv_obj_set_style_text_color(r, UI_PAL.accent, LV_PART_SELECTED);
}

static char *range_opts(int from, int to, bool pad, char *buf, size_t cap)
{
    size_t n = 0;
    for (int v = from; v <= to && n + 8 < cap; v++)
        n += (size_t)snprintf(buf + n, cap - n, pad ? "%02d%s" : "%d%s", v, v < to ? "\n" : "");
    return buf;
}

static void save_cb(lv_event_t *e)
{
    (void)e;
    app_state_t st;
    app_state_get(&st);
    if (st.rtc_valid && !ui_require(EL_ACT_SET_TIME, "set the clock")) return;   /* first set: anyone */
    el_datetime_t dt = {
        .year = (int16_t)(YEAR0 + lv_roller_get_selected(ST.r[0])),
        .month = (uint8_t)(lv_roller_get_selected(ST.r[1]) + 1),
        .day = (uint8_t)(lv_roller_get_selected(ST.r[2]) + 1),
        .hour = (uint8_t)lv_roller_get_selected(ST.r[3]),
        .minute = (uint8_t)lv_roller_get_selected(ST.r[4]),
        .second = 0,
    };
    int64_t local = 0;
    if (el_datetime_to_time(&dt, &local) != EL_OK) { ui_toast_locked("That date does not exist", 2500); return; }
    int64_t utc = local - (int64_t)ui_settings()->utc_offset_min * 60;
    if (ui_port_set_time(utc) != EL_OK) { ui_toast_locked("Could not set the clock", 2500); return; }
    ui_toast_locked("Clock set", 2000);
    ui_back();
}

static lv_obj_t *st_create(void)
{
    static const char *const CAP[5] = { "YEAR", "MONTH", "DAY", "HOUR", "MIN" };
    static const lv_coord_t W[5] = { 130, 150, 100, 100, 100 };
    static char years[8 * 80], days[3 * 32], hours[3 * 24], mins[3 * 60];
    lv_obj_t *s = ui_screen_new();
    ui_header(s, "Date & time", true);
    lv_obj_t *c = ui_card(s, UI_M, UI_TOP + 8, UI_W - 2 * UI_M, 226);
    lv_coord_t x = 32;
    for (int i = 0; i < 5; i++) {
        ui_caption(c, x + 4, 14, CAP[i]);
        ST.r[i] = lv_roller_create(c);
        roller_style(ST.r[i], W[i]);
        lv_obj_set_pos(ST.r[i], x, 38);
        x += W[i] + (i == 2 ? 48 : 20);
    }
    lv_roller_set_options(ST.r[0], range_opts(YEAR0, 2099, false, years, sizeof years), LV_ROLLER_MODE_NORMAL);
    lv_roller_set_options(ST.r[1], "Jan\nFeb\nMar\nApr\nMay\nJun\nJul\nAug\nSep\nOct\nNov\nDec", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_options(ST.r[2], range_opts(1, 31, false, days, sizeof days), LV_ROLLER_MODE_NORMAL);
    lv_roller_set_options(ST.r[3], range_opts(0, 23, true, hours, sizeof hours), LV_ROLLER_MODE_NORMAL);
    lv_roller_set_options(ST.r[4], range_opts(0, 59, true, mins, sizeof mins), LV_ROLLER_MODE_NORMAL);
    ST.now = ui_label_at(s, UI_F20, UI_PAL.text2, UI_M, 398, "");
    ST.tz = ui_label_at(s, UI_F16, UI_PAL.text3, UI_M, 430, "");
    ui_btn(s, UI_BTN_PRIMARY, LV_SYMBOL_OK, "Set clock", UI_W - UI_M - 260, 388, 260, 76, save_cb, NULL);
    return s;
}

static void st_show(const app_state_t *s)
{
    int64_t now = ui_port_now();
    if (!s->rtc_valid || now < 1704067200) now = 1767225600;   /* 2026-01-01 when unknown */
    el_datetime_t dt;
    el_time_to_datetime(now + (int64_t)ui_settings()->utc_offset_min * 60, &dt);
    int y = dt.year < YEAR0 ? YEAR0 : dt.year;
    lv_roller_set_selected(ST.r[0], (uint16_t)(y - YEAR0), LV_ANIM_OFF);
    lv_roller_set_selected(ST.r[1], (uint16_t)(dt.month - 1), LV_ANIM_OFF);
    lv_roller_set_selected(ST.r[2], (uint16_t)(dt.day - 1), LV_ANIM_OFF);
    lv_roller_set_selected(ST.r[3], dt.hour, LV_ANIM_OFF);
    lv_roller_set_selected(ST.r[4], dt.minute, LV_ANIM_OFF);
    int off = ui_settings()->utc_offset_min;
    char buf[96];
    snprintf(buf, sizeof buf, "Local time, UTC%c%02d:%02d. Offset: Settings > Display & power.",
             off < 0 ? '-' : '+', (off < 0 ? -off : off) / 60, (off < 0 ? -off : off) % 60);
    ui_set_text(ST.tz, buf);
}

static void st_refresh(const app_state_t *s)
{
    char buf[64], d[32];
    if (s->rtc_valid) { ui_fmt_datetime(ui_port_now(), d, sizeof d); snprintf(buf, sizeof buf, "Clock now: %s", d); }
    else snprintf(buf, sizeof buf, "The clock is not set. Results need the right time.");
    ui_set_text(ST.now, buf);
    lv_obj_set_style_text_color(ST.now, s->rtc_valid ? UI_PAL.text2 : ui_tone_text(EL_UI_TONE_BAD), 0);
}

const ui_scr_ops_t UI_SCR_SET_TIME = { st_create, st_show, st_refresh, NULL };
