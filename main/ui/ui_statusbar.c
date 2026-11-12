/* Status bar on the top layer (shared by every screen) and small shared formatters.
 *   left:  time, date, login level
 *   right: screen off, SIM, LOG (automatic logging on), Wi-Fi (tap -> Wi-Fi screen), USB, SD card */
#include <stdio.h>
#include <string.h>
#include "ui_screens.h"
#include "ui_port.h"
#include "el_ui_fmt.h"
#include "el_ui_status.h"

static lv_obj_t *s_bar, *s_time, *s_date, *s_level, *s_right;
static lv_obj_t *s_sim, *s_log, *s_sd, *s_wifi, *s_usb, *s_off;

/* ---------------- formatters ---------------- */

void ui_fmt_temp(float c, char *buf, size_t cap) { el_ui_fmt_fixed(c, ui_settings()->decimals, NULL, buf, cap); }
void ui_fmt_rh(float pct, char *buf, size_t cap)  { el_ui_fmt_fixed(pct, ui_settings()->decimals, NULL, buf, cap); }

/* "5.0-35.0 C  .  20-80 %RH"; a side at the end of the sensor range shows as "off". */
void ui_fmt_limits(const el_limits_t *l, char *buf, size_t cap)
{
    char a[16], b[16], c[16], d[16];
    if (l->t_low_c <= EL_TEMP_MIN_C) snprintf(a, sizeof a, "off"); else el_ui_fmt_fixed(l->t_low_c, 1, NULL, a, sizeof a);
    if (l->t_high_c >= EL_TEMP_MAX_C) snprintf(b, sizeof b, "off"); else el_ui_fmt_fixed(l->t_high_c, 1, NULL, b, sizeof b);
    if (l->rh_low_pct <= EL_RH_MIN_PCT) snprintf(c, sizeof c, "off"); else el_ui_fmt_fixed(l->rh_low_pct, 0, NULL, c, sizeof c);
    if (l->rh_high_pct >= EL_RH_MAX_PCT) snprintf(d, sizeof d, "off"); else el_ui_fmt_fixed(l->rh_high_pct, 0, NULL, d, sizeof d);
    snprintf(buf, cap, "%s \xE2\x80\x93 %s " UI_DEG_C "  \xC2\xB7  %s \xE2\x80\x93 %s %%RH", a, b, c, d);
}

void ui_fmt_datetime(int64_t t, char *buf, size_t cap)
{
    const el_settings_t *s = ui_settings();
    if (t <= 0) { snprintf(buf, cap, EL_UI_NA); return; }
    el_ui_fmt_datetime(t, s->utc_offset_min, s->date_format, buf, cap);
}

void ui_fmt_short(int64_t t, char *buf, size_t cap)
{
    if (t <= 0) { snprintf(buf, cap, EL_UI_NA); return; }
    el_ui_fmt_short(t, ui_settings()->utc_offset_min, buf, cap);
}

void ui_fmt_date(int64_t t, char *buf, size_t cap)
{
    const el_settings_t *s = ui_settings();
    if (t <= 0) { snprintf(buf, cap, EL_UI_NA); return; }
    el_ui_fmt_date(t, s->utc_offset_min, s->date_format, buf, cap);
}

const lv_img_dsc_t *ui_logo(void) { return ui_theme_is_dark() ? &ui_logo_dark : &ui_logo_light; }

/* ---------------- bar ---------------- */

static void sd_cb(lv_event_t *e) { (void)e; if (!ui_dialog_open()) ui_go(SCR_EXPORT); }
static void off_cb(lv_event_t *e) { (void)e; ui_screen_off(); }
static void menu_cb(lv_event_t *e) { (void)e; if (!ui_dialog_open() && !ui_nav_blocked()) ui_menu_dialog(); }
static void wifi_cb(lv_event_t *e) { (void)e; if (!ui_dialog_open()) ui_go(SCR_WIFI); }
static void level_cb(lv_event_t *e)
{
    (void)e;
    if (ui_dialog_open()) return;
    if (ui_level() > EL_LEVEL_OPERATOR) ui_logout();
    else ui_login_dialog(EL_LEVEL_OPERATOR);
}

static lv_obj_t *icon(lv_obj_t *parent)
{
    lv_obj_t *l = ui_label(parent, UI_F20, UI_PAL.bar_text, "");
    return l;
}

void ui_statusbar_create(void)
{
    s_bar = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_bar);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_bar, UI_W, UI_SB_H);
    lv_obj_set_style_bg_color(s_bar, UI_PAL.bar_bg, 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);

    /* Menu: the way to every page from every page (ui_menu_dialog). Stops at y = 48, where the
     * Back button in the header starts. */
    lv_obj_t *menu = lv_obj_create(s_bar);
    lv_obj_remove_style_all(menu);
    lv_obj_clear_flag(menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(menu, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(menu, 96, UI_SB_H);
    lv_obj_set_ext_click_area(menu, 4);
    lv_obj_add_event_cb(menu, menu_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(menu, UI_PAL.accent, 0);
    lv_obj_set_style_bg_opa(menu, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(menu, UI_PAL.accent_pressed, LV_STATE_PRESSED);
    lv_obj_t *menu_l = ui_label(menu, UI_F16, UI_PAL.on_accent, UI_ICON_LIST_UL " Menu");
    lv_obj_center(menu_l);

    s_time = ui_label_at(s_bar, UI_F24, UI_PAL.bar_text, UI_M, 0, "--:--");
    lv_obj_align(s_time, LV_ALIGN_LEFT_MID, 104, 0);
    s_date = ui_label(s_bar, UI_F16, UI_PAL.bar_text2, "");
    lv_obj_align_to(s_date, s_time, LV_ALIGN_OUT_RIGHT_MID, 12, 1);

    s_level = ui_chip(s_bar, EL_UI_TONE_NEUTRAL, false, UI_ICON_USER, "OPERATOR", UI_F14);
    lv_obj_add_flag(s_level, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_level, 8);
    lv_obj_add_event_cb(s_level, level_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_pos(s_level, 318, 9);

    s_right = lv_obj_create(s_bar);
    lv_obj_remove_style_all(s_right);
    lv_obj_clear_flag(s_right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_right, 470, UI_SB_H);
    lv_obj_align(s_right, LV_ALIGN_RIGHT_MID, -UI_M, 0);
    lv_obj_set_flex_flow(s_right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_right, 16, 0);

    s_off = ui_label(s_right, UI_F20, UI_PAL.bar_text2, UI_ICON_MOON);   /* screen off now */
    lv_obj_add_flag(s_off, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_off, 12);
    lv_obj_add_event_cb(s_off, off_cb, LV_EVENT_CLICKED, NULL);
    s_sim = ui_chip(s_right, EL_UI_TONE_FAULT, true, NULL, "SIM", UI_F14);
    s_log = ui_chip(s_right, EL_UI_TONE_ACCENT, true, LV_SYMBOL_REFRESH, "LOG", UI_F14);
    s_wifi = ui_label(s_right, UI_F20, UI_PAL.bar_text, LV_SYMBOL_WIFI);
    lv_obj_add_flag(s_wifi, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_wifi, 10);
    lv_obj_add_event_cb(s_wifi, wifi_cb, LV_EVENT_CLICKED, NULL);
    s_usb = icon(s_right);
    s_sd = ui_label(s_right, UI_F20, UI_PAL.bar_text, LV_SYMBOL_SD_CARD);
    lv_obj_add_flag(s_sd, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_sd, 10);
    lv_obj_add_event_cb(s_sd, sd_cb, LV_EVENT_CLICKED, NULL);
}

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void ui_statusbar_refresh(const app_state_t *s)
{
    if (!s_bar) return;
    const el_settings_t *set = ui_settings();
    char buf[48];
    int64_t now = ui_port_now();
    if (s->rtc_valid && now > 0) {
        el_ui_fmt_time(now, set->utc_offset_min, false, buf, sizeof buf);
        ui_set_text(s_time, buf);
        el_ui_fmt_date(now, set->utc_offset_min, set->date_format, buf, sizeof buf);
        ui_set_text(s_date, buf);
    } else {
        ui_set_text(s_time, "--:--");
        ui_set_text(s_date, "clock not set");
    }
    lv_obj_align_to(s_date, s_time, LV_ALIGN_OUT_RIGHT_MID, 12, 1);

    el_ui_badge_t b;
    el_ui_level_badge(s->ui_level, &b);
    ui_chip_set(s_level, b.tone, s->ui_level != EL_LEVEL_OPERATOR,
                s->ui_level == EL_LEVEL_OPERATOR ? UI_ICON_USER : UI_ICON_USER_SHIELD, b.text);

    show(s_sim, s->sensor_simulated);
    show(s_log, s->next_auto_s != UINT32_MAX);

    /* Wi-Fi: hidden when off; dim while connecting; warning colour on a problem */
    show(s_wifi, s->net_state != NET_OFF);
    lv_color_t wc = UI_PAL.bar_text;
    if (s->net_state == NET_CONNECTING) wc = UI_PAL.bar_text2;
    else if (s->net_state == NET_FAILED || s->net_state == NET_NOT_SET_UP || s->uplink_rejected)
        wc = ui_tone_fg(EL_UI_TONE_WARN);
    lv_obj_set_style_text_color(s_wifi, wc, 0);
    ui_set_text(s_usb, LV_SYMBOL_USB);
    show(s_usb, s->usb_pc_connected);
    ui_set_text(s_sd, s->export_busy ? LV_SYMBOL_SD_CARD " ..." : LV_SYMBOL_SD_CARD);
    show(s_sd, s->sd_present);
}
