/* HISTORY, TREND and EXPORT screens. History and Trend read the log through storage.h, one
 * page / one chart at a time (storage_log_iterate is slow: it reads flash). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ui_screens.h"
#include "ui_port.h"
#include "storage.h"
#include "el_ui_fmt.h"
#include "el_ui_status.h"
#include "el_ui_trend.h"

static void vis(lv_obj_t *o, bool on)
{
    if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* ======================================================================= HISTORY */

#define ROWS        5
#define ROW_H       50
#define PAGES_MAX   400

static struct {
    lv_obj_t *del, *empty, *pager, *prev, *next;
    lv_obj_t *row[ROWS], *c_time[ROWS], *c_temp[ROWS], *c_rh[ROWS], *c_dew[ROWS], *c_chip[ROWS];
    uint32_t  pstart[PAGES_MAX];           /* index (0 = oldest) each page starts at */
    uint32_t  page;
    bool      has_next;
    el_result_t res[ROWS];
    int       n;
} HI;

typedef struct { int n; uint32_t last; } page_ctx_t;

static bool page_cb(uint32_t index, const el_result_t *r, void *ctx)
{
    page_ctx_t *c = ctx;
    HI.res[c->n++] = *r;
    c->last = index;
    return c->n < ROWS;
}

static void hist_render(void)
{
    char buf[48], v[24];
    for (int i = 0; i < ROWS; i++) {
        if (i >= HI.n) { vis(HI.row[i], false); continue; }
        const el_result_t *r = &HI.res[i];
        vis(HI.row[i], true);
        ui_fmt_short(r->timestamp_s, buf, sizeof buf);
        ui_set_text(HI.c_time[i], buf);
        ui_fmt_temp(r->temp_c, v, sizeof v);
        snprintf(buf, sizeof buf, "%s " UI_DEG_C, v);
        ui_set_text(HI.c_temp[i], buf);
        ui_fmt_rh(r->rh_pct, v, sizeof v);
        snprintf(buf, sizeof buf, "%s %%", v);
        ui_set_text(HI.c_rh[i], buf);
        ui_fmt_temp(r->dew_point_c, v, sizeof v);
        snprintf(buf, sizeof buf, "%s " UI_DEG_C, v);
        ui_set_text(HI.c_dew[i], buf);
        el_ui_tone_t t = el_ui_status_tone(r->status);
        ui_chip_set(HI.c_chip[i], t, false, ui_glyph(el_ui_status_glyph(r->status)), el_ui_status_label(r->status));
    }
    uint32_t count = 0;
    storage_log_status(&count, NULL, NULL, NULL);
    vis(HI.empty, HI.n == 0);
    if (HI.n == 0) ui_set_text(HI.empty, count == 0 ? "No results yet" : "No more results");
    snprintf(buf, sizeof buf, "Page %lu of %lu  " "\xC2\xB7" "  %lu results", (unsigned long)HI.page + 1,
             (unsigned long)el_ui_page_count(count, ROWS), (unsigned long)count);
    ui_set_text(HI.pager, buf);
    ui_btn_enable(HI.prev, HI.page > 0);
    ui_btn_enable(HI.next, HI.has_next);
}

static void hist_load(void)
{
    uint32_t count = 0;
    storage_log_status(&count, NULL, NULL, NULL);
    if (HI.page == 0) HI.pstart[0] = count ? count - 1 : 0;
    page_ctx_t c = { 0, 0 };
    HI.n = 0;
    if (count) storage_log_iterate(HI.pstart[HI.page], ROWS, EL_DATALOG_NEWEST_FIRST, page_cb, &c);
    HI.n = c.n;
    HI.has_next = c.n == ROWS && c.last > 0 && HI.page + 1 < PAGES_MAX;
    if (HI.has_next) HI.pstart[HI.page + 1] = c.last - 1;
    hist_render();
}

static void prev_cb(lv_event_t *e) { (void)e; if (HI.page) { HI.page--; hist_load(); } }
static void next_cb(lv_event_t *e) { (void)e; if (HI.has_next) { HI.page++; hist_load(); } }

static void del_ok(void *u)
{
    (void)u;
    el_err_t e = storage_log_delete_all(ui_level());
    ui_toast_locked(e == EL_OK ? "All results deleted" : "Could not delete the log", 3000);
    HI.page = 0;
    hist_load();
}
static void del_cb(lv_event_t *e)
{
    (void)e;
    if (!ui_require(EL_ACT_DELETE_LOG, "delete results")) return;
    uint32_t count = 0;
    storage_log_status(&count, NULL, NULL, NULL);
    char msg[160];
    snprintf(msg, sizeof msg, "All %lu results will be removed from this logger. Export them first if you need them. "
             "This cannot be undone.", (unsigned long)count);
    ui_confirm_dialog("Delete all results?", msg, "Delete all", true, del_ok, NULL);
}

static void row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= HI.n) return;
    const el_result_t *r = &HI.res[i];
    static char v[9][64];
    static const char *const K[9] = { "Time", "Location", "Operator", "Temperature", "Humidity", "Dew point",
                                      "Readings / SD", "Status", "Log number" };
    const char *V[9];
    char a[24], b[24];
    ui_fmt_datetime(r->timestamp_s, v[0], 64);
    snprintf(v[1], 64, "%s", r->tag[0] ? r->tag : EL_UI_NA);
    snprintf(v[2], 64, "%s", r->operator_id[0] ? r->operator_id : ((r->flags & EL_RES_FLAG_AUTO) ? "automatic" : EL_UI_NA));
    ui_fmt_temp(r->temp_c, a, sizeof a);
    snprintf(v[3], 64, "%s " UI_DEG_C, a);
    ui_fmt_rh(r->rh_pct, a, sizeof a);
    snprintf(v[4], 64, "%s %%RH", a);
    ui_fmt_temp(r->dew_point_c, a, sizeof a);
    snprintf(v[5], 64, "%s " UI_DEG_C, a);
    if (r->repeats > 1) {
        el_ui_fmt_fixed(r->temp_sd_c, 2, NULL, a, sizeof a);
        el_ui_fmt_fixed(r->rh_sd_pct, 2, NULL, b, sizeof b);
        snprintf(v[6], 64, "%u / %s " UI_DEG_C ", %s %%", r->repeats, a, b);
    } else {
        snprintf(v[6], 64, "1 / " EL_UI_NA);
    }
    snprintf(v[7], 64, "%s%s%s%s", el_ui_status_label(r->status), r->alarms ? " - " : "",
             r->alarms ? el_ui_alarm_text(r->alarms) : "", (r->flags & EL_RES_FLAG_SIMULATED) ? "  SIM" : "");
    snprintf(v[8], 64, "%lu", (unsigned long)r->seq);
    for (int k = 0; k < 9; k++) V[k] = v[k];
    ui_info_dialog("Result details", K, V, 9);
}

static lv_obj_t *col(lv_obj_t *p, const lv_font_t *f, lv_coord_t x, lv_coord_t w, bool right)
{
    lv_obj_t *l = ui_label(p, f, UI_PAL.text, "");
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    if (right) lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, x, 0);
    return l;
}

static lv_obj_t *right_caption(lv_obj_t *card, lv_coord_t x, const char *text)
{
    lv_obj_t *cp = ui_caption(card, 0, 10, text);
    lv_obj_set_width(cp, 110);
    lv_obj_set_style_text_align(cp, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_x(cp, x);
    return cp;
}

static lv_obj_t *hist_create(void)
{
    lv_obj_t *s = ui_screen_new();
    lv_obj_t *h = ui_header(s, "History", true);
    HI.del = ui_btn(h, UI_BTN_DANGER, LV_SYMBOL_TRASH, NULL, 668, 4, 116, UI_TOUCH, del_cb, NULL);

    lv_obj_t *card = ui_card(s, UI_M, UI_TOP + 8, UI_W - 2 * UI_M, 36 + ROWS * ROW_H + 8);
    ui_caption(card, 16, 10, "TIME");
    right_caption(card, 166, "TEMP.");
    right_caption(card, 296, "HUMIDITY");
    right_caption(card, 426, "DEW POINT");
    ui_caption(card, 586, 10, "STATUS");
    for (int i = 0; i < ROWS; i++) {
        HI.row[i] = ui_row_btn(card, 4, 36 + i * ROW_H, UI_W - 2 * UI_M - 10, ROW_H, row_cb, (void *)(intptr_t)i);
        if (i) ui_hline(card, 12, 36 + i * ROW_H, UI_W - 2 * UI_M - 24);
        HI.c_time[i] = col(HI.row[i], UI_F20, 12, 150, false);
        HI.c_temp[i] = col(HI.row[i], UI_F24, 162, 110, true);
        HI.c_rh[i] = col(HI.row[i], UI_F24, 292, 110, true);
        HI.c_dew[i] = col(HI.row[i], UI_F20, 422, 110, true);
        HI.c_chip[i] = ui_chip(HI.row[i], EL_UI_TONE_NEUTRAL, false, NULL, "", UI_F14);
        lv_obj_align(HI.c_chip[i], LV_ALIGN_LEFT_MID, 578, 0);
    }
    HI.empty = ui_label(card, UI_F24, UI_PAL.text3, "");
    lv_obj_center(HI.empty);

    lv_coord_t y = UI_H - UI_TOUCH - 10;
    HI.prev = ui_btn(s, UI_BTN_SECONDARY, LV_SYMBOL_LEFT, "Newer", UI_M, y, 180, UI_TOUCH, prev_cb, NULL);
    HI.next = ui_btn(s, UI_BTN_SECONDARY, NULL, "Older  " LV_SYMBOL_RIGHT, UI_W - UI_M - 180, y, 180, UI_TOUCH, next_cb, NULL);
    HI.pager = ui_label(s, UI_F20, UI_PAL.text2, "");
    lv_obj_set_width(HI.pager, 380);
    lv_obj_set_style_text_align(HI.pager, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(HI.pager, (UI_W - 380) / 2, y + 20);
    return s;
}

static void hist_show(const app_state_t *s)
{
    (void)s;
    HI.page = 0;
    hist_load();
}

static void hist_event(app_event_id_t id, int32_t arg, const app_state_t *s)
{
    (void)arg; (void)s;
    if (id == EVT_LOG_CHANGED && HI.page == 0) hist_load();
}

const ui_scr_ops_t UI_SCR_HISTORY = { hist_create, hist_show, NULL, hist_event };

/* ======================================================================= TREND */

#define TR_MAX    300         /* newest results loaded */
#define TR_PTS    100         /* chart points after downsampling */
#define TR_SCALE  10.0f       /* chart units per degree / percent: tenths fit the 16-bit axis */

static struct {
    lv_obj_t *b_temp, *b_rh, *chart, *empty, *x_first, *x_last;
    lv_chart_series_t *ser, *s_low, *s_high;
    lv_obj_t *k_n, *k_last, *k_min, *k_max, *k_low, *k_high, *chip;
    bool      humidity;         /* which quantity is shown */
} TR;

static el_ui_pt_t s_pts[TR_MAX], s_ds[TR_PTS];
static size_t s_npts;
static el_result_status_t s_last_status;

static bool trend_cb(uint32_t index, const el_result_t *r, void *ctx)
{
    (void)index; (void)ctx;
    if (r->status == EL_RES_ERROR) return true;
    s_pts[s_npts].t = r->timestamp_s;
    s_pts[s_npts].v = TR.humidity ? r->rh_pct : r->temp_c;
    if (s_npts == 0) s_last_status = r->status;
    s_npts++;
    return s_npts < TR_MAX;
}

static void tick_label_cb(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *d = lv_event_get_draw_part_dsc(e);
    if (!d || d->part != LV_PART_TICKS || !d->text) return;
    if (d->id != LV_CHART_AXIS_PRIMARY_Y) { d->text[0] = '\0'; return; }
    int tenths = (int)d->value;              /* the axis is in tenths */
    int a = tenths < 0 ? -tenths : tenths;
    if (a % 10 == 0) lv_snprintf(d->text, d->text_length, "%d", tenths / 10);
    else lv_snprintf(d->text, d->text_length, "%s%d.%d", tenths < 0 ? "-" : "", a / 10, a % 10);
}

static lv_coord_t to_axis(float v) { return (lv_coord_t)lroundf(v * TR_SCALE); }

static void val_text(float v, char *buf, size_t cap)
{
    char t[24];
    if (TR.humidity) { ui_fmt_rh(v, t, sizeof t); snprintf(buf, cap, "%s %%RH", t); }
    else { ui_fmt_temp(v, t, sizeof t); snprintf(buf, cap, "%s " UI_DEG_C, t); }
}

static void trend_load(void)
{
    const el_settings_t *set = ui_settings();
    char buf[48];
    lv_obj_t *on = TR.humidity ? TR.b_rh : TR.b_temp, *off = TR.humidity ? TR.b_temp : TR.b_rh;
    lv_obj_set_style_bg_color(on, UI_PAL.accent, 0);
    lv_obj_set_style_text_color(on, UI_PAL.on_accent, 0);
    lv_obj_set_style_bg_color(off, UI_PAL.surface, 0);
    lv_obj_set_style_text_color(off, UI_PAL.text, 0);

    s_npts = 0;
    uint32_t count = 0;
    storage_log_status(&count, NULL, NULL, NULL);
    if (count) storage_log_iterate(count - 1, TR_MAX, EL_DATALOG_NEWEST_FIRST, trend_cb, NULL);
    /* newest first -> oldest first */
    for (size_t i = 0; i < s_npts / 2; i++) { el_ui_pt_t t = s_pts[i]; s_pts[i] = s_pts[s_npts - 1 - i]; s_pts[s_npts - 1 - i] = t; }

    el_limits_t lim = el_settings_limits(set);
    float lo_lim = TR.humidity ? lim.rh_low_pct : lim.t_low_c;
    float hi_lim = TR.humidity ? lim.rh_high_pct : lim.t_high_c;
    bool lo_on = TR.humidity ? lo_lim > EL_RH_MIN_PCT : lo_lim > EL_TEMP_MIN_C;
    bool hi_on = TR.humidity ? hi_lim < EL_RH_MAX_PCT : hi_lim < EL_TEMP_MAX_C;

    bool empty = s_npts == 0;
    vis(TR.empty, empty);
    if (empty) ui_set_text(TR.empty, "No results yet");

    size_t m = el_ui_downsample(s_pts, s_npts, s_ds, TR_PTS);
    float vmin = INFINITY, vmax = -INFINITY;
    for (size_t i = 0; i < s_npts; i++) {
        if (!isfinite(s_pts[i].v)) continue;
        if (s_pts[i].v < vmin) vmin = s_pts[i].v;
        if (s_pts[i].v > vmax) vmax = s_pts[i].v;
    }
    float amin = vmin, amax = vmax;
    if (empty) { amin = TR.humidity ? 0.0f : 10.0f; amax = TR.humidity ? 100.0f : 30.0f; }
    if (lo_on && lo_lim < amin) amin = lo_lim;   /* keep the limit lines on the chart */
    if (hi_on && hi_lim > amax) amax = hi_lim;
    float lo = 0, hi = 10, step = 2;
    el_ui_nice_range(amin, amax, 5, TR.humidity ? 2.0f : 1.0f, &lo, &hi, &step);
    lv_chart_set_range(TR.chart, LV_CHART_AXIS_PRIMARY_Y, to_axis(lo), to_axis(hi));
    uint16_t npts = m < 2 ? 2 : (uint16_t)m;
    lv_chart_set_point_count(TR.chart, npts);
    for (uint16_t i = 0; i < npts; i++) {
        float v = (i < m && !empty) ? s_ds[i].v : NAN;
        TR.ser->y_points[i] = isfinite(v) ? to_axis(v) : LV_CHART_POINT_NONE;
        TR.s_low->y_points[i] = lo_on ? to_axis(lo_lim) : LV_CHART_POINT_NONE;
        TR.s_high->y_points[i] = hi_on ? to_axis(hi_lim) : LV_CHART_POINT_NONE;
    }
    lv_chart_refresh(TR.chart);

    if (!empty) {
        ui_fmt_short(s_pts[0].t, buf, sizeof buf);
        ui_set_text(TR.x_first, buf);
        ui_fmt_short(s_pts[s_npts - 1].t, buf, sizeof buf);
        ui_set_text(TR.x_last, buf);
        val_text(s_pts[s_npts - 1].v, buf, sizeof buf);
        ui_set_text(TR.k_last, buf);
        val_text(vmin, buf, sizeof buf);
        ui_set_text(TR.k_min, buf);
        val_text(vmax, buf, sizeof buf);
        ui_set_text(TR.k_max, buf);
        el_ui_tone_t t = el_ui_status_tone(s_last_status);
        ui_chip_set(TR.chip, t, true, ui_glyph(el_ui_status_glyph(s_last_status)), el_ui_status_label(s_last_status));
    } else {
        ui_set_text(TR.x_first, "");
        ui_set_text(TR.x_last, "");
        ui_set_text(TR.k_last, EL_UI_NA);
        ui_set_text(TR.k_min, EL_UI_NA);
        ui_set_text(TR.k_max, EL_UI_NA);
    }
    vis(TR.chip, !empty);
    snprintf(buf, sizeof buf, "%lu%s", (unsigned long)s_npts, s_npts == TR_MAX ? " (latest)" : "");
    ui_set_text(TR.k_n, buf);
    if (lo_on) val_text(lo_lim, buf, sizeof buf); else snprintf(buf, sizeof buf, "Low: off");
    ui_set_text(TR.k_low, buf);
    if (hi_on) val_text(hi_lim, buf, sizeof buf); else snprintf(buf, sizeof buf, "High: off");
    ui_set_text(TR.k_high, buf);
}

static void quantity_cb(lv_event_t *e)
{
    TR.humidity = lv_event_get_user_data(e) != NULL;
    trend_load();
}

static lv_obj_t *trend_create(void)
{
    lv_obj_t *s = ui_screen_new();
    lv_obj_t *h = ui_header(s, "Trend", true);
    TR.b_temp = ui_btn(h, UI_BTN_SECONDARY, UI_ICON_THERMO, "Temperature", 404, 4, 200, UI_TOUCH, quantity_cb, NULL);
    TR.b_rh = ui_btn(h, UI_BTN_SECONDARY, UI_ICON_TINT, "Humidity", 614, 4, 170, UI_TOUCH, quantity_cb, (void *)1);

    lv_obj_t *card = ui_card(s, UI_M, UI_TOP + 8, 560, UI_H - UI_TOP - 8 - UI_M);
    TR.chart = lv_chart_create(card);
    lv_obj_set_pos(TR.chart, 64, 16);
    lv_obj_set_size(TR.chart, 480, 284);
    lv_chart_set_type(TR.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(TR.chart, 6, 0);
    lv_chart_set_axis_tick(TR.chart, LV_CHART_AXIS_PRIMARY_Y, 6, 3, 6, 1, true, 56);
    lv_obj_add_event_cb(TR.chart, tick_label_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    lv_obj_set_style_bg_opa(TR.chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(TR.chart, 0, 0);
    lv_obj_set_style_pad_all(TR.chart, 4, 0);
    lv_obj_set_style_line_color(TR.chart, UI_PAL.border, LV_PART_MAIN);
    lv_obj_set_style_text_font(TR.chart, UI_F14, LV_PART_TICKS);
    lv_obj_set_style_text_color(TR.chart, UI_PAL.text2, LV_PART_TICKS);
    lv_obj_set_style_line_width(TR.chart, 3, LV_PART_ITEMS);
    lv_obj_set_style_size(TR.chart, 0, LV_PART_INDICATOR);   /* lines only */
    TR.s_high = lv_chart_add_series(TR.chart, ui_tone_fg(EL_UI_TONE_BAD), LV_CHART_AXIS_PRIMARY_Y);
    TR.s_low = lv_chart_add_series(TR.chart, ui_tone_fg(EL_UI_TONE_BAD), LV_CHART_AXIS_PRIMARY_Y);
    TR.ser = lv_chart_add_series(TR.chart, UI_PAL.accent, LV_CHART_AXIS_PRIMARY_Y);
    TR.x_first = ui_label_at(card, UI_F14, UI_PAL.text3, 64, 306, "");
    TR.x_last = ui_label(card, UI_F14, UI_PAL.text3, "");
    lv_obj_set_width(TR.x_last, 200);
    lv_obj_set_style_text_align(TR.x_last, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(TR.x_last, 344, 306);
    TR.empty = ui_label(card, UI_F24, UI_PAL.text3, "");
    lv_obj_center(TR.empty);

    lv_obj_t *st = ui_card(s, 588, UI_TOP + 8, UI_W - 588 - UI_M, UI_H - UI_TOP - 8 - UI_M);
    ui_caption(st, 16, 12, "LATEST");
    TR.chip = ui_chip(st, EL_UI_TONE_NEUTRAL, true, NULL, "", UI_F16);
    lv_obj_set_pos(TR.chip, 14, 34);
    TR.k_last = ui_label_at(st, UI_F24, UI_PAL.text, 16, 72, "");
    ui_caption(st, 16, 112, "LOWEST / HIGHEST");
    TR.k_min = ui_label_at(st, UI_F20, UI_PAL.text, 16, 132, "");
    TR.k_max = ui_label_at(st, UI_F20, UI_PAL.text, 16, 158, "");
    ui_caption(st, 16, 192, "RESULTS");
    TR.k_n = ui_label_at(st, UI_F20, UI_PAL.text, 16, 212, "");
    ui_label_at(st, UI_F16, ui_tone_text(EL_UI_TONE_BAD), 16, 248, "\xE2\x80\x94  Alarm limits");
    TR.k_low = ui_label_at(st, UI_F16, UI_PAL.text, 16, 272, "");
    TR.k_high = ui_label_at(st, UI_F16, UI_PAL.text, 16, 296, "");
    return s;
}

static void trend_show(const app_state_t *s) { (void)s; trend_load(); }

static void trend_event(app_event_id_t id, int32_t arg, const app_state_t *s)
{
    (void)arg; (void)s;
    if (id == EVT_LOG_CHANGED || id == EVT_SETTINGS_CHANGED) trend_load();
}

const ui_scr_ops_t UI_SCR_TREND = { trend_create, trend_show, NULL, trend_event };

/* ======================================================================= EXPORT */

static struct {
    lv_obj_t *card, *state, *scope_btn[2], *bar, *status, *go, *last;
    int       scope;                        /* storage_export_scope_t */
} EX;

static void ex_render(const app_state_t *s)
{
    bool present = s->sd_present;
    lv_obj_set_style_border_color(EX.card, present ? UI_PAL.accent : UI_PAL.border, 0);
    lv_obj_set_style_border_width(EX.card, present ? 3 : 1, 0);
    ui_set_text(EX.state, present ? "Ready" : "No card. Insert a FAT-formatted microSD card.");
    lv_obj_set_style_text_color(EX.state, present ? ui_tone_text(EL_UI_TONE_OK) : UI_PAL.text2, 0);
    for (int i = 0; i < 2; i++) {
        bool sel = EX.scope == i;
        lv_obj_set_style_bg_color(EX.scope_btn[i], sel ? UI_PAL.accent : UI_PAL.surface, 0);
        lv_obj_set_style_text_color(EX.scope_btn[i], sel ? UI_PAL.on_accent : UI_PAL.text, 0);
        lv_obj_set_style_border_color(EX.scope_btn[i], sel ? UI_PAL.accent : UI_PAL.border, 0);
    }
    char buf[112];
    bool busy = s->export_busy;
    lv_bar_set_value(EX.bar, busy ? s->export_progress_pct : 0, LV_ANIM_OFF);
    vis(EX.bar, busy);
    if (busy) {
        snprintf(buf, sizeof buf, "Exporting... %u%%  Do not remove the card.", s->export_progress_pct);
        ui_set_text(EX.status, buf);
    } else if (!present) {
        ui_set_text(EX.status, "Waiting for an SD card");
    } else {
        uint32_t n = 0;
        storage_log_status(&n, NULL, NULL, NULL);
        snprintf(buf, sizeof buf, "%lu results in the log", (unsigned long)n);
        ui_set_text(EX.status, buf);
    }
    ui_btn_enable(EX.go, present && !busy);
    ui_btn_set_text(EX.go, busy ? NULL : LV_SYMBOL_SD_CARD, busy ? "Exporting..." : "Export");

    storage_export_result_t r;
    storage_export_last(&r);
    if (r.file[0]) snprintf(buf, sizeof buf, "Last export: %s (%lu rows)", r.file, (unsigned long)r.rows);
    else buf[0] = '\0';
    ui_set_text(EX.last, buf);
}

static void scope_cb(lv_event_t *e)
{
    EX.scope = (int)(intptr_t)lv_event_get_user_data(e);
    app_state_t s;
    app_state_get(&s);
    ex_render(&s);
}
static void go_cb(lv_event_t *e)
{
    (void)e;
    if (!ui_require(EL_ACT_EXPORT, "export")) return;
    app_events_post(EVT_REQ_EXPORT, SRC_UI, EX.scope);
}

static lv_obj_t *ex_create(void)
{
    lv_obj_t *s = ui_screen_new();
    ui_header(s, "Export", true);
    EX.card = ui_card(s, UI_M, UI_TOP + 8, UI_W - 2 * UI_M, 128);
    ui_label_at(EX.card, UI_F48, UI_PAL.accent, 24, 32, LV_SYMBOL_SD_CARD);
    ui_label_at(EX.card, UI_F24, UI_PAL.text, 96, 20, "SD card");
    EX.state = ui_label_at(EX.card, UI_F16, UI_PAL.text2, 96, 56, "");
    lv_obj_set_width(EX.state, 620);
    lv_label_set_long_mode(EX.state, LV_LABEL_LONG_WRAP);
    ui_label_at(EX.card, UI_F14, UI_PAL.text3, 96, 84, "One CSV file per export, one row per result");

    ui_caption(s, UI_M + 4, 270, "WHAT TO EXPORT");
    static const char *const SC[2] = { "New since last export", "Everything" };
    static const int SCV[2] = { STORAGE_EXPORT_SINCE_LAST, STORAGE_EXPORT_ALL };
    for (int i = 0; i < 2; i++) {
        EX.scope_btn[SCV[i]] = ui_btn(s, UI_BTN_SECONDARY, NULL, SC[i], UI_M + i * (300 + 12), 292, 300, UI_TOUCH,
                                      scope_cb, (void *)(intptr_t)SCV[i]);
        lv_obj_set_style_border_width(EX.scope_btn[SCV[i]], 1, 0);
        lv_obj_set_style_text_font(lv_obj_get_child(EX.scope_btn[SCV[i]], 0), UI_F20, 0);
    }
    EX.bar = lv_bar_create(s);
    lv_obj_set_pos(EX.bar, UI_M, 392);
    lv_obj_set_size(EX.bar, 500, 16);
    lv_bar_set_range(EX.bar, 0, 100);
    lv_obj_set_style_bg_color(EX.bar, UI_PAL.surface2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(EX.bar, UI_PAL.accent, LV_PART_INDICATOR);
    EX.status = ui_label_at(s, UI_F20, UI_PAL.text, UI_M, 418, "");
    EX.last = ui_label_at(s, UI_F14, UI_PAL.text3, UI_M, 448, "");
    lv_obj_set_width(EX.last, 500);
    lv_label_set_long_mode(EX.last, LV_LABEL_LONG_DOT);
    EX.go = ui_btn(s, UI_BTN_PRIMARY, LV_SYMBOL_SD_CARD, "Export", 540, 388, UI_W - 540 - UI_M, 76, go_cb, NULL);
    EX.scope = STORAGE_EXPORT_SINCE_LAST;
    return s;
}

const ui_scr_ops_t UI_SCR_EXPORT = { ex_create, ex_render, ex_render, NULL };
