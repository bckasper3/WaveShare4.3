/* Screen manager, 500 ms tick, idle dim/off, touchscreen login session, toasts. */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "ui_screens.h"
#include "ui_port.h"
#include "storage.h"
#include "el_ui_idle.h"
#include "el_ui_status.h"
#include "el_app_console.h"
#include "app_config.h"

#define TICK_MS     500
#define UI_BOOT_MIN_MS 1500     /* the logo stays up at least this long */
#define NAV_DEPTH   8

static const ui_scr_ops_t *const OPS[SCR_COUNT] = {
    [SCR_HOME] = &UI_SCR_HOME,           [SCR_MEASURING] = &UI_SCR_MEASURING, [SCR_RESULT] = &UI_SCR_RESULT,
    [SCR_HISTORY] = &UI_SCR_HISTORY,     [SCR_TREND] = &UI_SCR_TREND,         [SCR_EXPORT] = &UI_SCR_EXPORT,
    [SCR_CALIBRATE] = &UI_SCR_CALIBRATE, [SCR_SETTINGS] = &UI_SCR_SETTINGS,   [SCR_DIAG] = &UI_SCR_DIAG,
    [SCR_ABOUT] = &UI_SCR_ABOUT,         [SCR_SET_TIME] = &UI_SCR_SET_TIME,   [SCR_WIFI] = &UI_SCR_WIFI,
};

static lv_obj_t     *s_scr[SCR_COUNT];
static ui_screen_t   s_cur = SCR_HOME;
static ui_screen_t   s_stack[NAV_DEPTH];
static int           s_depth;
static el_settings_t s_set;
static el_session_t  s_sess;
static el_level_t    s_level_pub = EL_LEVEL_OPERATOR;
static el_ui_idle_t  s_idle;
static lv_obj_t     *s_wake;             /* catches the touch that wakes a dark screen */
static lv_obj_t     *s_toast;
static lv_timer_t   *s_toast_timer;
static uint32_t      s_last_inactive;
static bool          s_forced_off;       /* screen switched off by the user */
static lv_obj_t     *s_boot;             /* boot screen while not NULL */
static uint32_t      s_boot_ms;
static uint8_t       s_theme_applied;

/* ---------------- navigation ---------------- */

static void load(ui_screen_t s, const app_state_t *st)
{
    if (!s_scr[s]) s_scr[s] = OPS[s]->create();
    s_cur = s;
    if (OPS[s]->show) OPS[s]->show(st);
    if (OPS[s]->refresh) OPS[s]->refresh(st);
    lv_scr_load(s_scr[s]);
}

/* Screens that are never returned to with Back (they describe a moment, not a place). */
static bool transient(ui_screen_t s) { return s == SCR_MEASURING || s == SCR_RESULT; }

void ui_go(ui_screen_t s)
{
    if (s >= SCR_COUNT || s_boot || (s == s_cur && s_scr[s] && lv_scr_act() == s_scr[s])) return;
    if (s == SCR_HOME) {
        s_depth = 0;
    } else if (!transient(s_cur)) {
        if (s_depth == NAV_DEPTH) { memmove(s_stack, s_stack + 1, sizeof s_stack[0] * (NAV_DEPTH - 1)); s_depth--; }
        s_stack[s_depth++] = s_cur;
    }
    app_state_t st;
    app_state_get(&st);
    load(s, &st);
}

void ui_back(void)
{
    ui_screen_t to = s_depth > 0 ? s_stack[--s_depth] : SCR_HOME;
    app_state_t st;
    app_state_get(&st);
    load(to, &st);
}

bool ui_nav_blocked(void)
{
    if (s_boot) return true;
    app_state_t st;
    app_state_get(&st);
    /* Only a reading the user started owns the screen; an automatic one runs in the background. */
    if (st.meas_state != EL_MEAS_MEASURING || st.meas_source == EL_MEAS_SRC_AUTO) return false;
    ui_toast_locked("Measurement running - cancel it first", 2500);
    return true;
}

ui_screen_t ui_current(void) { return s_cur; }
const el_settings_t *ui_settings(void) { return &s_set; }

/* ---------------- login ---------------- */

static void publish_level(void)
{
    el_level_t l = el_session_level(&s_sess, ui_port_ms());
    if (l == s_level_pub) return;
    if (l < s_level_pub && s_level_pub != EL_LEVEL_OPERATOR) ui_toast_locked("Logged out", 2500);
    s_level_pub = l;
    app_state_lock();
    app_state_raw()->ui_level = l;
    app_state_unlock();
}

el_level_t ui_level(void) { return el_session_level(&s_sess, ui_port_ms()); }
bool ui_allowed(el_action_t a) { return el_access_allows(ui_level(), a); }

bool ui_require(el_action_t a, const char *what)
{
    if (ui_allowed(a)) return true;
    char buf[80];
    el_level_t need = el_action_min_level(a);
    snprintf(buf, sizeof buf, "Log in as %s to %s", need == EL_LEVEL_ENGINEER ? "Engineer" : "Admin", what);
    ui_toast_locked(buf, 3000);
    ui_login_dialog(need);
    return false;
}

static void login_result(el_login_t r, el_level_t want, uint32_t retry_ms)
{
    char buf[80];
    switch (r) {
    case EL_LOGIN_OK:
        snprintf(buf, sizeof buf, "Logged in as %s", want == EL_LEVEL_ENGINEER ? "Engineer" : "Admin");
        storage_event_log("UI login %s", el_level_name(want));
        break;
    case EL_LOGIN_WRONG:    snprintf(buf, sizeof buf, "Wrong PIN"); break;
    case EL_LOGIN_LOCKED:   snprintf(buf, sizeof buf, "Too many tries - wait %lu s", (unsigned long)((retry_ms + 999) / 1000)); break;
    case EL_LOGIN_DISABLED: snprintf(buf, sizeof buf, "Engineer login is disabled in this build"); break;
    default:                snprintf(buf, sizeof buf, "Login failed"); break;
    }
    ui_toast_locked(buf, 2500);
    publish_level();
    app_state_t st;
    app_state_get(&st);
    if (OPS[s_cur]->show) OPS[s_cur]->show(&st);       /* screens re-check permissions */
    if (OPS[s_cur]->refresh) OPS[s_cur]->refresh(&st);
}

static bool pin_entered(const char *text, void *user)
{
    el_level_t want = (el_level_t)(intptr_t)user;
    uint32_t retry = 0;
    el_login_t r;
    if (want == EL_LEVEL_ENGINEER)
        r = el_session_login(&s_sess, want, text, s_set.engineer_code, ui_port_ms(), &retry);
    else
        r = el_session_login(&s_sess, want, text, s_set.admin_pin, ui_port_ms(), &retry);
    login_result(r, want, retry);
    return r != EL_LOGIN_WRONG;      /* wrong PIN: keep the pad open for another try */
}

static void start_login(el_level_t want)
{
    if (want == EL_LEVEL_ADMIN && s_set.admin_pin[0] == '\0') {   /* no PIN set: any entry passes */
        uint32_t retry = 0;
        login_result(el_session_login(&s_sess, want, "", "", ui_port_ms(), &retry), want, retry);
        return;
    }
    if (want == EL_LEVEL_ENGINEER && !ui_port_engineer_login_enabled()) {
        login_result(EL_LOGIN_DISABLED, want, 0);
        return;
    }
    ui_text_dialog(want == EL_LEVEL_ENGINEER ? "Engineer code" : "Admin PIN", "", EL_UI_CS_DIGITS,
                   want == EL_LEVEL_ENGINEER ? EL_CODE_MAX_LEN : EL_SET_PIN_MAX, true, pin_entered,
                   (void *)(intptr_t)want);
}

static void level_picked(int idx, void *user) { (void)user; start_login(idx == 1 ? EL_LEVEL_ENGINEER : EL_LEVEL_ADMIN); }

void ui_login_dialog(el_level_t want)
{
    if (want == EL_LEVEL_OPERATOR) {
        static const char *const OPT[] = { "Admin", "Engineer" };
        ui_choice_dialog("Log in as", OPT, ui_port_engineer_login_enabled() ? 2 : 1, -1, level_picked, NULL);
        return;
    }
    start_login(want);
}

void ui_logout(void)
{
    el_session_logout(&s_sess);
    publish_level();
    app_state_t st;
    app_state_get(&st);
    if (OPS[s_cur]->show) OPS[s_cur]->show(&st);
}

/* ---------------- toast ---------------- */

static void toast_kill(void)
{
    if (s_toast_timer) { lv_timer_del(s_toast_timer); s_toast_timer = NULL; }
    if (s_toast) { lv_obj_del(s_toast); s_toast = NULL; }
}
static void toast_timer_cb(lv_timer_t *t) { (void)t; s_toast_timer = NULL; toast_kill(); }
static void toast_click_cb(lv_event_t *e) { (void)e; toast_kill(); }

void ui_toast_locked(const char *msg, uint32_t ms)
{
    toast_kill();
    s_toast = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_toast);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(s_toast, UI_W - 80, 0);
    lv_obj_set_style_bg_color(s_toast, UI_PAL.bar_bg, 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_toast, UI_R, 0);
    lv_obj_set_style_pad_hor(s_toast, 24, 0);
    lv_obj_set_style_pad_ver(s_toast, 14, 0);
    lv_obj_set_style_shadow_width(s_toast, 24, 0);
    lv_obj_set_style_shadow_opa(s_toast, LV_OPA_30, 0);
    lv_obj_t *l = ui_label(s_toast, UI_F24, UI_PAL.bar_text, msg);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_width(l, UI_W - 128, 0);
    lv_obj_set_width(l, LV_SIZE_CONTENT);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_add_event_cb(s_toast, toast_click_cb, LV_EVENT_CLICKED, NULL);
    if (ms) s_toast_timer = lv_timer_create(toast_timer_cb, ms, NULL);
    if (s_toast_timer) lv_timer_set_repeat_count(s_toast_timer, 1);
}

void ui_toast(const char *msg, uint32_t ms)
{
    if (!ui_port_lock(200)) return;
    ui_toast_locked(msg, ms);
    ui_port_unlock();
}

/* ---------------- idle dim / off ---------------- */

static void apply_backlight(void) { ui_port_backlight(el_ui_idle_backlight_pct(s_idle.state, s_set.brightness_pct)); }

static void wake_cb(lv_event_t *e)
{
    (void)e;
    s_forced_off = false;
    if (s_wake) { lv_obj_del_async(s_wake); s_wake = NULL; }
    el_ui_idle_activity(&s_idle, ui_port_ms());
    apply_backlight();
}

static void add_wake_catcher(void)
{
    if (s_wake) return;
    s_wake = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(s_wake);
    lv_obj_set_size(s_wake, UI_W, UI_H);
    lv_obj_add_flag(s_wake, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wake, wake_cb, LV_EVENT_PRESSED, NULL);
}

void ui_screen_off(void)
{
    ui_dialog_close();
    s_forced_off = true;
    s_idle.state = EL_UI_PWR_OFF;
    ui_port_backlight(0);
    add_wake_catcher();
}

static void idle_tick(const app_state_t *st)
{
    uint32_t now = ui_port_ms();
    if (s_forced_off) { s_last_inactive = lv_disp_get_inactive_time(NULL); return; }
    uint32_t inactive = lv_disp_get_inactive_time(NULL);
    if (inactive < s_last_inactive) {              /* a touch since the last tick */
        el_ui_idle_activity(&s_idle, now);
        el_session_touch(&s_sess, now);
        if (s_idle.state == EL_UI_PWR_ON) apply_backlight();
    }
    s_last_inactive = inactive;
    bool user_run = st->meas_state == EL_MEAS_MEASURING && st->meas_source != EL_MEAS_SRC_AUTO;
    bool keep = user_run || st->export_busy || s_cur == SCR_MEASURING || s_boot;
    bool changed = false;
    el_ui_pwr_t p = el_ui_idle_tick(&s_idle, now, s_set.idle_dim_s, s_set.idle_off_s, keep, &changed);
    if (!changed) return;
    apply_backlight();
    if (p == EL_UI_PWR_OFF && !s_wake) {
        add_wake_catcher();
    } else if (p != EL_UI_PWR_OFF && s_wake) {
        lv_obj_del(s_wake);
        s_wake = NULL;
    }
}

static void wake_locked(void)
{
    s_forced_off = false;
    if (s_wake) { lv_obj_del(s_wake); s_wake = NULL; }
    el_ui_idle_activity(&s_idle, ui_port_ms());
    apply_backlight();
}

void ui_activity(void)
{
    if (!ui_port_lock(200)) return;
    wake_locked();
    ui_port_unlock();
}

/* ---------------- theme switch ---------------- */

/* Every widget took its colours at creation: rebuild all screens and the status bar. */
static void rebuild_theme(void)
{
    ui_dialog_close();
    toast_kill();
    s_theme_applied = s_set.theme;
    ui_theme_init(s_set.theme == EL_THEME_DARK);
    lv_obj_t *old = lv_scr_act();
    for (int i = 0; i < SCR_COUNT; i++) {
        if (s_scr[i] && s_scr[i] != old) lv_obj_del(s_scr[i]);
        s_scr[i] = NULL;
    }
    lv_obj_clean(lv_layer_top());               /* status bar */
    ui_statusbar_create();
    app_state_t st;
    app_state_get(&st);
    load(s_cur, &st);
    if (old != lv_scr_act()) lv_obj_del(old);
    ui_statusbar_refresh(&st);
}

/* ---------------- boot screen ---------------- */

static void boot_create(void)
{
    s_boot = ui_screen_new();
    lv_obj_t *logo = lv_img_create(s_boot);
    lv_img_set_src(logo, ui_logo());
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -50);
    lv_obj_t *t = ui_label(s_boot, UI_F24, UI_PAL.text2, "Temperature & humidity logger");
    lv_obj_align_to(t, logo, LV_ALIGN_OUT_BOTTOM_MID, 0, 24);
    lv_obj_t *bar = lv_bar_create(s_boot);
    lv_obj_set_size(bar, 240, 6);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 70);
    lv_obj_set_style_bg_color(bar, UI_PAL.surface2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, UI_PAL.accent, LV_PART_INDICATOR);
    lv_obj_set_style_anim_time(bar, UI_BOOT_MIN_MS, 0);
    lv_bar_set_value(bar, 100, LV_ANIM_ON);
    char buf[96];
    snprintf(buf, sizeof buf, "Device %s  \xC2\xB7  Firmware %s", s_set.device_serial, ui_port_fw_version());
    lv_obj_t *f = ui_label(s_boot, UI_F16, UI_PAL.text3, buf);
    lv_obj_align(f, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_add_flag(lv_layer_top(), LV_OBJ_FLAG_HIDDEN);   /* no status bar over the logo */
    lv_scr_load(s_boot);
}

static void boot_leave(lv_timer_t *t)
{
    if (t) lv_timer_del(t);
    if (!s_boot) return;
    lv_obj_clear_flag(lv_layer_top(), LV_OBJ_FLAG_HIDDEN);
    app_state_t st;
    app_state_get(&st);
    lv_obj_t *boot = s_boot;
    s_boot = NULL;
    s_depth = 0;
    load(SCR_HOME, &st);
    if (!st.rtc_valid) ui_go(SCR_SET_TIME);
    lv_obj_del(boot);
    ui_statusbar_refresh(&st);
    if (st.boot_selftest_fail) {
        char buf[64];
        snprintf(buf, sizeof buf, "Start-up check: %u problem%s - see Diagnostics", st.boot_selftest_fail,
                 st.boot_selftest_fail == 1 ? "" : "s");
        ui_toast_locked(buf, 5000);
    }
}

void ui_boot_done(void)
{
    if (!ui_port_lock(500)) return;
    uint32_t el = ui_port_ms() - s_boot_ms;
    if (el >= UI_BOOT_MIN_MS) boot_leave(NULL);
    else lv_timer_create(boot_leave, UI_BOOT_MIN_MS - el, NULL);
    ui_port_unlock();
}

/* ---------------- tick + events ---------------- */

static void refresh_all(const app_state_t *st)
{
    ui_statusbar_refresh(st);
    if (!s_boot && s_scr[s_cur] && OPS[s_cur]->refresh) OPS[s_cur]->refresh(st);
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    app_state_t st;
    app_state_get(&st);
    publish_level();
    idle_tick(&st);
    if (s_boot) return;
    /* Back to Home a while after a result (setting home_return_s; 0 = stay). */
    if (s_cur == SCR_RESULT && s_set.home_return_s && !ui_dialog_open() && !st.result_unsaved &&
        lv_disp_get_inactive_time(NULL) >= (uint32_t)s_set.home_return_s * 1000u)
        ui_go(SCR_HOME);
    refresh_all(&st);
}

void ui_notify(app_event_id_t id, int32_t arg)
{
    if (!ui_port_lock(200)) return;
    app_state_t st;
    app_state_get(&st);
    char buf[96];
    switch (id) {
    case EVT_MEAS_STARTED:
    case EVT_MEAS_PROGRESS:
        /* An automatic reading runs in the background: the screen stays where it is. */
        if (s_boot || st.meas_source == EL_MEAS_SRC_AUTO) break;
        if (s_cur != SCR_MEASURING) { wake_locked(); ui_dialog_close(); ui_go(SCR_MEASURING); }
        break;
    case EVT_MEAS_FAULT:
        if (st.meas_source == EL_MEAS_SRC_AUTO) {
            snprintf(buf, sizeof buf, "Automatic reading failed: %s", el_ui_fault_title((el_meas_fault_t)arg));
            ui_toast_locked(buf, 4000);
            break;
        }
        /* fall through */
    case EVT_MEAS_DONE:
        if (id == EVT_MEAS_DONE && (st.last_result.flags & EL_RES_FLAG_AUTO)) {
            if (st.last_result.status == EL_RES_ALARM) {          /* an alarm wakes the screen */
                wake_locked();
                snprintf(buf, sizeof buf, "Alarm: %s", el_ui_alarm_text(st.last_result.alarms));
                ui_toast_locked(buf, 6000);
            }
            if (s_cur == SCR_RESULT && OPS[SCR_RESULT]->show) OPS[SCR_RESULT]->show(&st);
            break;
        }
        wake_locked();
        ui_dialog_close();
        if (s_cur == SCR_RESULT) { if (OPS[SCR_RESULT]->show) OPS[SCR_RESULT]->show(&st); }
        else ui_go(SCR_RESULT);
        break;
    case EVT_MEAS_CANCELLED:
        if (s_cur == SCR_MEASURING) ui_go(SCR_HOME);
        ui_toast_locked("Measurement cancelled", 2500);
        break;
    case EVT_MEAS_REJECTED:
        if (st.meas_source != EL_MEAS_SRC_AUTO || s_cur == SCR_HOME)
            ui_toast_locked(el_ui_reject_text((el_meas_reject_t)arg), 3500);
        break;
    case EVT_LOG_APPEND_FAILED:
        ui_toast_locked("Result not saved yet - retrying", 3500);
        break;
    case EVT_SD_MOUNTED:          ui_toast_locked("SD card ready", 2000); break;
    case EVT_SD_REMOVED:          ui_toast_locked("SD card removed", 2500); break;
    case EVT_EXPORT_DONE: {
        storage_export_result_t r;
        storage_export_last(&r);
        if (arg == 0 && r.nothing_to_export) snprintf(buf, sizeof buf, "Nothing new to export");
        else if (arg == 0) snprintf(buf, sizeof buf, "Exported %lu results - safe to remove", (unsigned long)r.rows);
        else if (r.err == EL_ERR_SIZE) snprintf(buf, sizeof buf, "Export failed: SD card is full");
        else snprintf(buf, sizeof buf, "Export failed - check the SD card and try again");
        ui_toast_locked(buf, 5000);
        break;
    }
    case EVT_TIME_SET:
        break;
    case EVT_SETTINGS_CHANGED:
        storage_settings_get(&s_set);
        s_sess.idle_timeout_ms = (uint32_t)s_set.login_idle_s * 1000u;
        apply_backlight();
        if (s_set.theme != s_theme_applied && !s_boot) rebuild_theme();
        break;
    default:
        break;
    }
    if (!s_boot && s_scr[s_cur] && OPS[s_cur]->event) OPS[s_cur]->event(id, arg, &st);
    app_state_get(&st);
    refresh_all(&st);
    ui_port_unlock();
}

void ui_show(ui_screen_t s)
{
    if (!ui_port_lock(500)) return;
    ui_go(s);
    ui_port_unlock();
}

esp_err_t ui_init(void)
{
    if (!ui_port_lock(1000)) return ESP_ERR_TIMEOUT;
    storage_settings_get(&s_set);
    el_session_init(&s_sess, (uint32_t)s_set.login_idle_s * 1000u, ui_port_ms());
    el_ui_idle_init(&s_idle, ui_port_ms());
    s_theme_applied = s_set.theme;
    ui_theme_init(s_set.theme == EL_THEME_DARK);
    lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_TRANSP, 0);
    ui_statusbar_create();
    s_boot_ms = ui_port_ms();
    boot_create();
    lv_timer_create(tick_cb, TICK_MS, NULL);
    apply_backlight();
    ui_port_unlock();
    return ESP_OK;
}
