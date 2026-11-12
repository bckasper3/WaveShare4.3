/* HOME, MEASURING and RESULT screens. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ui_screens.h"
#include "ui_port.h"
#include "storage.h"
#include "el_ui_fmt.h"
#include "el_ui_status.h"

static void post(app_event_id_t id) { app_events_post(id, SRC_UI, 0); }

static void vis(lv_obj_t *o, bool on)
{
    if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* A big value with its unit to the right, both bottom-aligned at (x, y_base). */
static void place_value(lv_obj_t *val, lv_obj_t *unit, lv_coord_t x, lv_coord_t y)
{
    lv_obj_set_pos(val, x, y);
    lv_obj_update_layout(val);
    lv_obj_align_to(unit, val, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -10);
}

/* ======================================================================= HOME */

static struct {
    lv_obj_t *card, *chip, *t_val, *t_unit, *h_val, *h_unit, *dew, *ago, *none, *banner;
    lv_obj_t *interval, *next, *location, *limits, *measure;
} H;

static void measure_cb(lv_event_t *e) { (void)e; post(EVT_REQ_MEASURE); }
static void last_cb(lv_event_t *e)
{
    (void)e;
    app_state_t s;
    app_state_get(&s);
    if (s.has_result) ui_go(SCR_RESULT);
}
static void nav_cb(lv_event_t *e) { ui_go((ui_screen_t)(intptr_t)lv_event_get_user_data(e)); }

static lv_obj_t *nav_btn(lv_obj_t *p, lv_coord_t x, const char *icon, const char *text, ui_screen_t to)
{
    lv_obj_t *b = ui_btn(p, UI_BTN_SECONDARY, NULL, NULL, x, 388, 118, 76, nav_cb, (void *)(intptr_t)to);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, UI_PAL.border, 0);
    lv_obj_t *i = ui_label(b, UI_F24, UI_PAL.accent, icon);
    lv_obj_align(i, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_t *t = ui_label(b, UI_F16, UI_PAL.text, text);
    lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -8);
    return b;
}

static lv_obj_t *home_create(void)
{
    lv_obj_t *s = ui_screen_new();
    H.card = ui_card(s, UI_M, 60, 470, 316);
    lv_obj_add_flag(H.card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(H.card, UI_PAL.surface2, LV_STATE_PRESSED);
    lv_obj_add_event_cb(H.card, last_cb, LV_EVENT_CLICKED, NULL);
    ui_caption(H.card, 20, 12, "LATEST READING");
    H.chip = ui_chip(H.card, EL_UI_TONE_NEUTRAL, true, NULL, "", UI_F16);
    lv_obj_align(H.chip, LV_ALIGN_TOP_RIGHT, -16, 8);
    ui_label_at(H.card, UI_F24, UI_PAL.accent, 22, 62, UI_ICON_THERMO);
    H.t_val = ui_label(H.card, UI_FD72, UI_PAL.text, "");
    H.t_unit = ui_label(H.card, UI_F32, UI_PAL.text2, UI_DEG_C);
    ui_label_at(H.card, UI_F24, UI_PAL.accent, 18, 150, UI_ICON_TINT);
    H.h_val = ui_label(H.card, UI_FD72, UI_PAL.text, "");
    H.h_unit = ui_label(H.card, UI_F32, UI_PAL.text2, "%RH");
    H.dew = ui_label_at(H.card, UI_F20, UI_PAL.text2, 20, 232, "");
    H.ago = ui_label_at(H.card, UI_F16, UI_PAL.text3, 20, 262, "");
    lv_obj_set_size(H.ago, 430, UI_F16->line_height);
    lv_label_set_long_mode(H.ago, LV_LABEL_LONG_DOT);
    H.none = ui_label(H.card, UI_F20, UI_PAL.text3, "No reading yet.\nPress MEASURE NOW or wait for\nthe next automatic reading.");
    lv_obj_set_style_text_align(H.none, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(H.none, LV_ALIGN_CENTER, 0, 0);
    H.banner = ui_chip(H.card, EL_UI_TONE_WARN, true, LV_SYMBOL_WARNING, "", UI_F14);
    lv_obj_align(H.banner, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_flag(H.banner, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *c = ui_card(s, 498, 60, 286, 200);
    ui_caption(c, 20, 12, "LOGGING");
    H.interval = ui_kv(c, 20, 40, 246, "Interval", "", UI_F16);
    H.next = ui_kv(c, 20, 70, 246, "Next", "", UI_F16);
    H.location = ui_kv(c, 20, 100, 246, "Location", "", UI_F16);
    ui_hline(c, 20, 132, 246);
    ui_caption(c, 20, 142, "ALARM LIMITS");
    H.limits = ui_label_at(c, UI_F16, UI_PAL.text2, 20, 162, "");
    lv_obj_set_width(H.limits, 250);

    H.measure = ui_btn(s, UI_BTN_PRIMARY, UI_ICON_THERMO, "MEASURE NOW", 498, 272, 286, 104, measure_cb, NULL);

    lv_coord_t x = UI_M;
    nav_btn(s, x, UI_ICON_HISTORY, "History", SCR_HISTORY);      x += 130;
    nav_btn(s, x, UI_ICON_CHART_LINE, "Trend", SCR_TREND);       x += 130;
    nav_btn(s, x, UI_ICON_HDD, "Export", SCR_EXPORT);            x += 130;
    nav_btn(s, x, UI_ICON_BALANCE, "Calibrate", SCR_CALIBRATE);  x += 130;
    nav_btn(s, x, LV_SYMBOL_WIFI, "Wi-Fi", SCR_WIFI);            x += 130;
    nav_btn(s, x, UI_ICON_SLIDERS, "Settings", SCR_SETTINGS);
    return s;
}

static void home_refresh(const app_state_t *s)
{
    char buf[96], v[32];
    const el_settings_t *set = ui_settings();

    /* logging card */
    if (set->log_interval_s) { el_ui_fmt_secs(set->log_interval_s, v, sizeof v); ui_set_text(H.interval, v); }
    else ui_set_text(H.interval, "Off");
    if (s->next_auto_s == UINT32_MAX) ui_set_text(H.next, "Manual only");
    else if (s->meas_state == EL_MEAS_MEASURING && s->meas_source == EL_MEAS_SRC_AUTO) ui_set_text(H.next, "Now");
    else { el_ui_fmt_secs(s->next_auto_s, v, sizeof v); snprintf(buf, sizeof buf, "in %s", v); ui_set_text(H.next, buf); }
    ui_set_text(H.location, set->location[0] ? set->location : "(not set)");
    el_limits_t lim = el_settings_limits(set);
    ui_fmt_limits(&lim, buf, sizeof buf);
    ui_set_text(H.limits, buf);

    const char *warn = NULL;
    if (s->log_full) warn = "Log full - export, then clear";
    else if (s->last_fault != EL_MEAS_FAULT_NONE) warn = "Last reading failed - check the sensor";
    else if (s->log_near_full) warn = "Log almost full - export soon";
    if (warn) { ui_chip_set(H.banner, EL_UI_TONE_WARN, true, LV_SYMBOL_WARNING, warn); lv_obj_align(H.banner, LV_ALIGN_BOTTOM_MID, 0, -8); }
    vis(H.banner, warn != NULL);

    /* latest reading */
    bool has = s->has_result;
    lv_obj_t *items[] = { H.chip, H.t_val, H.t_unit, H.h_val, H.h_unit, H.dew, H.ago };
    for (size_t i = 0; i < sizeof items / sizeof items[0]; i++) vis(items[i], has);
    vis(H.none, !has);
    if (!has) return;

    const el_result_t *r = &s->last_result;
    el_ui_tone_t t = el_ui_status_tone(r->status);
    ui_chip_set(H.chip, t, true, ui_glyph(el_ui_status_glyph(r->status)), el_ui_status_label(r->status));
    lv_obj_align(H.chip, LV_ALIGN_TOP_RIGHT, -16, 8);
    ui_fmt_temp(r->temp_c, v, sizeof v);
    ui_set_text(H.t_val, v);
    ui_fmt_rh(r->rh_pct, v, sizeof v);
    ui_set_text(H.h_val, v);
    lv_color_t tc = (r->alarms & (EL_ALARM_T_LOW | EL_ALARM_T_HIGH)) ? ui_tone_text(EL_UI_TONE_BAD) : UI_PAL.text;
    lv_color_t hc = (r->alarms & (EL_ALARM_RH_LOW | EL_ALARM_RH_HIGH)) ? ui_tone_text(EL_UI_TONE_BAD) : UI_PAL.text;
    lv_obj_set_style_text_color(H.t_val, tc, 0);
    lv_obj_set_style_text_color(H.h_val, hc, 0);
    place_value(H.t_val, H.t_unit, 60, 44);
    place_value(H.h_val, H.h_unit, 60, 132);
    ui_fmt_temp(r->dew_point_c, v, sizeof v);
    snprintf(buf, sizeof buf, "Dew point %s " UI_DEG_C, v);
    ui_set_text(H.dew, buf);
    el_ui_fmt_ago(ui_port_now(), r->timestamp_s, v, sizeof v);
    snprintf(buf, sizeof buf, "%s%s%s%s", v, (r->flags & EL_RES_FLAG_AUTO) ? "  " "\xC2\xB7" "  automatic" : "",
             r->status == EL_RES_ALARM ? "  " "\xC2\xB7" "  " : "", r->status == EL_RES_ALARM ? el_ui_alarm_text(r->alarms) : "");
    if (s->result_unsaved) { size_t n = strlen(buf); snprintf(buf + n, sizeof buf - n, "  " "\xC2\xB7" "  NOT SAVED YET"); }
    ui_set_text(H.ago, buf);
}

const ui_scr_ops_t UI_SCR_HOME = { home_create, NULL, home_refresh, NULL };

/* ======================================================================= MEASURING */

static struct { lv_obj_t *arc, *pct, *phase, *repeat, *location, *sim, *cancel; } M;

static void cancel_cb(lv_event_t *e) { (void)e; post(EVT_REQ_CANCEL); }

static lv_obj_t *meas_create(void)
{
    lv_obj_t *s = ui_screen_new();
    M.arc = lv_arc_create(s);
    lv_obj_set_size(M.arc, 300, 300);
    lv_obj_set_pos(M.arc, 56, 96);
    lv_arc_set_rotation(M.arc, 270);
    lv_arc_set_bg_angles(M.arc, 0, 360);
    lv_arc_set_range(M.arc, 0, 100);
    lv_obj_remove_style(M.arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(M.arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(M.arc, 24, LV_PART_MAIN);
    lv_obj_set_style_arc_width(M.arc, 24, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(M.arc, UI_PAL.surface2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(M.arc, UI_PAL.accent, LV_PART_INDICATOR);
    M.pct = ui_label(s, UI_F48, UI_PAL.text, "0%");
    lv_obj_align_to(M.pct, M.arc, LV_ALIGN_CENTER, 0, 0);

    M.phase = ui_label_at(s, UI_F32, UI_PAL.text, 420, 110, "Measuring");
    M.repeat = ui_label_at(s, UI_F24, UI_PAL.text2, 420, 160, "");
    M.location = ui_kv(s, 420, 230, 364, "Location", "", UI_F24);
    M.sim = ui_chip(s, EL_UI_TONE_FAULT, false, UI_ICON_MICROCHIP, "Simulated sensor", UI_F16);
    lv_obj_set_pos(M.sim, 420, 286);
    M.cancel = ui_btn(s, UI_BTN_DANGER, LV_SYMBOL_CLOSE, "Cancel", 420, 388, 364, 76, cancel_cb, NULL);
    return s;
}

static void meas_refresh(const app_state_t *s)
{
    char buf[48];
    lv_arc_set_value(M.arc, s->meas_progress_pct);
    snprintf(buf, sizeof buf, "%u%%", s->meas_progress_pct);
    ui_set_text(M.pct, buf);
    lv_obj_align_to(M.pct, M.arc, LV_ALIGN_CENTER, 0, 0);
    ui_set_text(M.phase, s->meas_state == EL_MEAS_MEASURING ? "Measuring" : "Finishing");
    if (s->meas_repeats > 1) snprintf(buf, sizeof buf, "Reading %u of %u", s->meas_repeat, s->meas_repeats);
    else snprintf(buf, sizeof buf, "Single reading");
    ui_set_text(M.repeat, buf);
    const char *loc = ui_settings()->location;
    ui_set_text(M.location, loc[0] ? loc : "(not set)");
    vis(M.sim, s->sensor_simulated);
}

const ui_scr_ops_t UI_SCR_MEASURING = { meas_create, NULL, meas_refresh, NULL };

/* ======================================================================= RESULT */

#define RES_ROWS 7

static struct {
    lv_obj_t *panel, *chip, *flags, *t_val, *t_unit, *h_val, *h_unit, *dew, *limits;
    lv_obj_t *f_icon, *f_title, *f_hint;
    lv_obj_t *det, *k[RES_ROWS];
    lv_obj_t *b_left, *b_mid, *b_right;
    bool fault;
} R;

static void home_cb(lv_event_t *e) { (void)e; ui_go(SCR_HOME); }
static void again_cb(lv_event_t *e) { (void)e; post(EVT_REQ_MEASURE); }
static void history_cb(lv_event_t *e) { (void)e; ui_go(SCR_HISTORY); }
static void diag_cb(lv_event_t *e) { (void)e; ui_go(SCR_DIAG); }

static void discard_ok(void *u) { (void)u; post(EVT_REQ_DISCARD_RESULT); ui_go(SCR_HOME); }
static void discard_cb(lv_event_t *e)
{
    (void)e;
    ui_confirm_dialog("Discard this result?", "It could not be saved to the log. Discarding it loses it for good.",
                      "Discard", true, discard_ok, NULL);
}

static void set_btn(lv_obj_t *b, ui_btn_kind_t kind, const char *icon, const char *text, lv_event_cb_t cb)
{
    lv_obj_remove_event_cb(b, NULL);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    ui_btn_set_text(b, icon, text);
    lv_color_t bg = UI_PAL.surface, fg = UI_PAL.text, bd = UI_PAL.border;
    if (kind == UI_BTN_PRIMARY) { bg = UI_PAL.accent; fg = UI_PAL.on_accent; bd = UI_PAL.accent; }
    else if (kind == UI_BTN_DANGER) { fg = ui_tone_text(EL_UI_TONE_BAD); bd = ui_tone_fg(EL_UI_TONE_BAD); }
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_text_color(b, fg, 0);
    lv_obj_set_style_border_color(b, bd, 0);
    lv_obj_set_style_border_width(b, kind == UI_BTN_PRIMARY ? 0 : 2, 0);
}

static lv_obj_t *res_create(void)
{
    static const char *const KEYS[RES_ROWS] = { "Location", "Time", "Readings", "Temp. SD", "RH SD",
                                                "Dew point", "Alarm" };
    lv_obj_t *s = ui_screen_new();
    R.panel = ui_card(s, UI_M, 60, 456, 316);
    lv_obj_set_style_border_width(R.panel, 2, 0);
    R.chip = ui_chip(R.panel, EL_UI_TONE_NEUTRAL, true, NULL, "", UI_F24);
    lv_obj_set_pos(R.chip, 18, 16);
    R.flags = lv_obj_create(R.panel);
    lv_obj_remove_style_all(R.flags);
    lv_obj_set_size(R.flags, 200, 40);
    lv_obj_align(R.flags, LV_ALIGN_TOP_RIGHT, -16, 18);
    lv_obj_set_flex_flow(R.flags, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(R.flags, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(R.flags, 6, 0);
    R.t_val = ui_label(R.panel, UI_FD72, UI_PAL.text, "");
    R.t_unit = ui_label(R.panel, UI_F32, UI_PAL.text2, UI_DEG_C);
    R.h_val = ui_label(R.panel, UI_FD72, UI_PAL.text, "");
    R.h_unit = ui_label(R.panel, UI_F32, UI_PAL.text2, "%RH");
    R.dew = ui_label_at(R.panel, UI_F20, UI_PAL.text2, 24, 238, "");
    R.limits = ui_label(R.panel, UI_F16, UI_PAL.text2, "");
    lv_obj_align(R.limits, LV_ALIGN_BOTTOM_MID, 0, -14);

    R.f_icon = ui_label_at(R.panel, UI_F48, UI_PAL.text, 24, 80, UI_ICON_EXCL_CIRCLE);
    R.f_title = ui_label_at(R.panel, UI_F32, UI_PAL.text, 24, 150, "");
    lv_obj_set_width(R.f_title, 408);
    lv_label_set_long_mode(R.f_title, LV_LABEL_LONG_WRAP);
    R.f_hint = ui_label_at(R.panel, UI_F20, UI_PAL.text2, 24, 200, "");
    lv_obj_set_width(R.f_hint, 408);
    lv_label_set_long_mode(R.f_hint, LV_LABEL_LONG_WRAP);

    R.det = ui_card(s, 484, 60, 300, 316);
    for (int i = 0; i < RES_ROWS; i++) {
        R.k[i] = ui_kv(R.det, 18, 20 + i * 41, 264, KEYS[i], "", NULL);
        if (i) ui_hline(R.det, 18, 10 + i * 41, 264);
    }

    R.b_left = ui_btn(s, UI_BTN_SECONDARY, NULL, "", UI_M, 388, 220, 76, home_cb, NULL);
    R.b_mid = ui_btn(s, UI_BTN_SECONDARY, NULL, "", UI_M + 232, 388, 256, 76, again_cb, NULL);
    R.b_right = ui_btn(s, UI_BTN_PRIMARY, NULL, "", UI_M + 500, 388, 268, 76, history_cb, NULL);
    return s;
}

static void flag_chip(el_ui_tone_t t, const char *text) { ui_chip(R.flags, t, false, NULL, text, UI_F14); }

static void res_show(const app_state_t *s)
{
    char buf[160], v[32];
    R.fault = s->meas_state == EL_MEAS_FAULT || (!s->has_result && s->last_fault != EL_MEAS_FAULT_NONE);
    lv_obj_clean(R.flags);
    lv_obj_t *norm[] = { R.t_val, R.t_unit, R.h_val, R.h_unit, R.dew, R.limits, R.flags };
    for (size_t i = 0; i < sizeof norm / sizeof norm[0]; i++) vis(norm[i], !R.fault);
    vis(R.f_icon, R.fault); vis(R.f_title, R.fault); vis(R.f_hint, R.fault);

    if (R.fault) {
        el_ui_tone_t t = EL_UI_TONE_FAULT;
        lv_obj_set_style_bg_color(R.panel, ui_tone_tint(t), 0);
        lv_obj_set_style_border_color(R.panel, ui_tone_fg(t), 0);
        ui_chip_set(R.chip, t, true, UI_ICON_EXCL_CIRCLE, "NO RESULT");
        lv_obj_set_style_text_color(R.f_icon, ui_tone_fg(t), 0);
        ui_set_text(R.f_title, el_ui_fault_title(s->last_fault));
        snprintf(buf, sizeof buf, "%s\nCode: %s", el_ui_fault_hint(s->last_fault), el_meas_fault_name(s->last_fault));
        ui_set_text(R.f_hint, buf);
        const char *loc = ui_settings()->location;
        ui_set_text(R.k[0], loc[0] ? loc : "(not set)");
        for (int i = 1; i < RES_ROWS; i++) ui_set_text(R.k[i], EL_UI_NA);
        lv_obj_set_style_text_color(R.k[6], UI_PAL.text, 0);
        set_btn(R.b_left, UI_BTN_SECONDARY, UI_ICON_TH_LARGE, "Home", home_cb);
        set_btn(R.b_mid, UI_BTN_SECONDARY, UI_ICON_STETHOSCOPE, "Diagnostics", diag_cb);
        set_btn(R.b_right, UI_BTN_PRIMARY, UI_ICON_REDO, "Try again", again_cb);
        return;
    }
    if (!s->has_result) return;
    const el_result_t *r = &s->last_result;
    el_ui_tone_t t = el_ui_status_tone(r->status);
    lv_obj_set_style_bg_color(R.panel, ui_tone_tint(t), 0);
    lv_obj_set_style_border_color(R.panel, ui_tone_fg(t), 0);
    ui_chip_set(R.chip, t, true, ui_glyph(el_ui_status_glyph(r->status)), el_ui_status_label(r->status));
    ui_fmt_temp(r->temp_c, v, sizeof v);
    ui_set_text(R.t_val, v);
    ui_fmt_rh(r->rh_pct, v, sizeof v);
    ui_set_text(R.h_val, v);
    lv_color_t tc = (r->alarms & (EL_ALARM_T_LOW | EL_ALARM_T_HIGH)) ? ui_tone_text(EL_UI_TONE_BAD) : UI_PAL.text;
    lv_color_t hc = (r->alarms & (EL_ALARM_RH_LOW | EL_ALARM_RH_HIGH)) ? ui_tone_text(EL_UI_TONE_BAD) : UI_PAL.text;
    lv_obj_set_style_text_color(R.t_val, tc, 0);
    lv_obj_set_style_text_color(R.h_val, hc, 0);
    place_value(R.t_val, R.t_unit, 40, 74);
    place_value(R.h_val, R.h_unit, 40, 156);
    ui_fmt_temp(r->dew_point_c, v, sizeof v);
    snprintf(buf, sizeof buf, "Dew point %s " UI_DEG_C, v);
    ui_set_text(R.dew, buf);
    ui_fmt_limits(&r->limits, buf, sizeof buf);
    ui_set_text(R.limits, buf);
    lv_obj_align(R.limits, LV_ALIGN_BOTTOM_MID, 0, -14);

    if (s->result_unsaved) flag_chip(EL_UI_TONE_BAD, "NOT SAVED");
    if (r->flags & EL_RES_FLAG_SIMULATED) flag_chip(EL_UI_TONE_FAULT, "SIM");
    if (r->flags & EL_RES_FLAG_AUTO) flag_chip(EL_UI_TONE_NEUTRAL, "AUTO");
    if (r->flags & EL_RES_FLAG_ADJUSTED) flag_chip(EL_UI_TONE_ACCENT, "ADJ");

    ui_set_text(R.k[0], r->tag[0] ? r->tag : "(not set)");
    ui_fmt_short(r->timestamp_s, buf, sizeof buf);
    ui_set_text(R.k[1], buf);
    snprintf(buf, sizeof buf, "%u", r->repeats);
    ui_set_text(R.k[2], buf);
    if (r->repeats > 1) {
        el_ui_fmt_fixed(r->temp_sd_c, 2, UI_DEG_C, buf, sizeof buf);
        ui_set_text(R.k[3], buf);
        el_ui_fmt_fixed(r->rh_sd_pct, 2, "%RH", buf, sizeof buf);
        ui_set_text(R.k[4], buf);
    } else {
        ui_set_text(R.k[3], EL_UI_NA);
        ui_set_text(R.k[4], EL_UI_NA);
    }
    ui_fmt_temp(r->dew_point_c, v, sizeof v);
    snprintf(buf, sizeof buf, "%s " UI_DEG_C, v);
    ui_set_text(R.k[5], buf);
    ui_set_text(R.k[6], r->alarms ? el_ui_alarm_text(r->alarms) : "None");
    lv_obj_set_style_text_color(R.k[6], r->alarms ? ui_tone_text(EL_UI_TONE_BAD) : UI_PAL.text, 0);

    set_btn(R.b_left, UI_BTN_SECONDARY, UI_ICON_TH_LARGE, "Home", home_cb);
    if (s->result_unsaved) set_btn(R.b_mid, UI_BTN_DANGER, LV_SYMBOL_TRASH, "Discard", discard_cb);
    else set_btn(R.b_mid, UI_BTN_SECONDARY, UI_ICON_REDO, "Measure again", again_cb);
    set_btn(R.b_right, UI_BTN_PRIMARY, UI_ICON_HISTORY, "History", history_cb);
}

static void res_refresh(const app_state_t *s)
{
    /* Re-render when the save state changes (retry succeeded / discarded). */
    static bool last_unsaved;
    if (s->result_unsaved != last_unsaved) { last_unsaved = s->result_unsaved; res_show(s); }
}

const ui_scr_ops_t UI_SCR_RESULT = { res_create, res_show, res_refresh, NULL };
