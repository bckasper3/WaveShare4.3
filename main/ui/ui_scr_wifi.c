/* WIFI: send-only result upload (docs/WIFI.md).
 *   left  STATUS  state chip, network, signal, IP, server, last upload, waiting to send, problem
 *   right SETUP   Wi-Fi on/off, network (scan + pick), password, server, access key, internet clock
 *   header        Send test
 * Anyone can look; changing anything needs Admin (the settings' own write level). */
#include <stdio.h>
#include <string.h>
#include "ui_screens.h"
#include "ui_port.h"
#include "storage.h"
#include "el_uplink.h"
#include "el_ui_settings.h"
#include "el_ui_fmt.h"

#define ROW_H   46
#define N_ROWS  6
#define SCAN_MAX 20

static const el_set_id_t ROW_ID[N_ROWS] = {
    EL_SET_WIFI_ENABLED, EL_SET_WIFI_SSID, EL_SET_WIFI_PASSWORD,
    EL_SET_UPLOAD_URL, EL_SET_UPLOAD_KEY, EL_SET_WIFI_TIME_SYNC,
};
static const char *const ROW_TITLE[N_ROWS] = {
    "Wi-Fi", "Network", "Password", "Upload server", "Access key", "Clock from internet",
};

static struct {
    lv_obj_t *chip, *k[6], *msg, *val[N_ROWS], *test;
    bool      scan_wait;
    ui_wifi_ap_t aps[SCAN_MAX];
    int       n_aps;
} W;

/* ---------------------------------------------------------------- editing */

static bool apply(el_set_id_t id, const char *text)
{
    el_err_t e = storage_settings_set(id, text, ui_level());
    if (e == EL_OK || e == EL_ERR_IO) return true;
    if (e == EL_ERR_DENIED) ui_toast_locked("Log in as Admin to change Wi-Fi", 3000);
    else if (id == EL_SET_UPLOAD_URL) ui_toast_locked("Use https://server/path (no spaces)", 3500);
    else if (id == EL_SET_WIFI_PASSWORD) ui_toast_locked("Password: 8 to 63 characters, or empty for an open network", 3500);
    else ui_toast_locked("Not a valid value", 3000);
    return false;
}

static bool text_set(const char *text, void *user) { return apply((el_set_id_t)(intptr_t)user, text); }

static bool can_edit(void)
{
    if (el_ui_set_writable(EL_SET_WIFI_ENABLED, ui_level())) return true;
    ui_require(EL_ACT_EDIT_SETTINGS, "change Wi-Fi");
    return false;
}

static void ask_password(bool needed)
{
    ui_text_dialog(needed ? "Wi-Fi password" : "Wi-Fi password (empty = open network)", "", EL_UI_CS_TEXT,
                   EL_UPLINK_PASS_MAX, true, text_set, (void *)(intptr_t)EL_SET_WIFI_PASSWORD);
}

static void ask_password_async(void *user) { (void)user; ask_password(false); }

static bool ssid_typed(const char *text, void *user)
{
    (void)user;
    if (!apply(EL_SET_WIFI_SSID, text)) return false;
    lv_async_call(ask_password_async, NULL);   /* open it after this dialog has closed */
    return true;
}

static void ap_picked(int idx, void *user)
{
    (void)user;
    if (idx == W.n_aps) {              /* "Type a network name" (hidden network) */
        ui_text_dialog("Network name", "", EL_UI_CS_TEXT, EL_UPLINK_SSID_MAX, false, ssid_typed, NULL);
        return;
    }
    if (idx < 0 || idx >= W.n_aps) return;
    if (!apply(EL_SET_WIFI_SSID, W.aps[idx].ssid)) return;
    if (W.aps[idx].secure) ask_password(true);
    else apply(EL_SET_WIFI_PASSWORD, "");
}

static void show_scan_list(void)
{
    static char text[SCAN_MAX][56];
    static const char *opts[SCAN_MAX + 1];
    for (int i = 0; i < W.n_aps; i++) {
        int r = W.aps[i].rssi;
        const char *q = r > -60 ? "excellent" : r > -70 ? "good" : r > -80 ? "fair" : "weak";
        snprintf(text[i], sizeof text[i], "%s%s  (%s)", W.aps[i].secure ? UI_ICON_LOCK " " : "", W.aps[i].ssid, q);
        opts[i] = text[i];
    }
    opts[W.n_aps] = "+ Type a network name";
    ui_choice_dialog(W.n_aps ? "Choose a Wi-Fi network" : "No networks found", opts, W.n_aps + 1, -1, ap_picked, NULL);
}

static void row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (!can_edit()) return;
    const el_settings_t *s = ui_settings();
    switch (ROW_ID[i]) {
    case EL_SET_WIFI_ENABLED:
        apply(EL_SET_WIFI_ENABLED, s->wifi_enabled ? "0" : "1");
        break;
    case EL_SET_WIFI_TIME_SYNC:
        apply(EL_SET_WIFI_TIME_SYNC, s->wifi_time_sync ? "0" : "1");
        break;
    case EL_SET_WIFI_SSID:
        if (ui_port_wifi_scan_start()) {
            W.scan_wait = true;
            ui_toast_locked("Searching for networks...", 2500);
        }
        break;
    case EL_SET_WIFI_PASSWORD:
        ask_password(false);
        break;
    case EL_SET_UPLOAD_URL:
        ui_text_dialog("Upload server, e.g. https://example.com/readings", s->upload_url, EL_UI_CS_TEXT,
                       EL_UPLINK_URL_MAX, false, text_set, (void *)(intptr_t)EL_SET_UPLOAD_URL);
        break;
    case EL_SET_UPLOAD_KEY:
        ui_text_dialog("Server access key (empty = none)", "", EL_UI_CS_TEXT, EL_UPLINK_TOKEN_MAX, true, text_set,
                       (void *)(intptr_t)EL_SET_UPLOAD_KEY);
        break;
    default:
        break;
    }
}

static void test_cb(lv_event_t *e)
{
    (void)e;
    const el_settings_t *s = ui_settings();
    if (!s->wifi_enabled) { ui_toast_locked("Turn Wi-Fi on first", 2500); return; }
    if (!s->upload_url[0]) { ui_toast_locked("Set the upload server first", 2500); return; }
    ui_port_wifi_send_test();
    ui_toast_locked("Test message queued - watch \"Last upload\"", 3000);
}

/* ---------------------------------------------------------------- render */

static void render(const app_state_t *st)
{
    const el_settings_t *s = ui_settings();
    static const struct { const char *text; el_ui_tone_t tone; } STATE[] = {
        [NET_OFF] = { "Off", EL_UI_TONE_NEUTRAL },        [NET_NOT_SET_UP] = { "Not set up", EL_UI_TONE_WARN },
        [NET_CONNECTING] = { "Connecting", EL_UI_TONE_ACCENT }, [NET_CONNECTED] = { "Connected", EL_UI_TONE_OK },
        [NET_FAILED] = { "Can't connect", EL_UI_TONE_BAD },
    };
    net_state_t ns = st->net_state <= NET_FAILED ? st->net_state : NET_OFF;
    ui_chip_set(W.chip, STATE[ns].tone, true, LV_SYMBOL_WIFI, STATE[ns].text);

    char buf[96];
    ui_set_text(W.k[0], s->wifi_ssid[0] ? s->wifi_ssid : "None chosen");
    if (ns == NET_CONNECTED && st->net_rssi) {
        int r = st->net_rssi;
        snprintf(buf, sizeof buf, "%s (%d dBm)", r > -60 ? "Excellent" : r > -70 ? "Good" : r > -80 ? "Fair" : "Weak", r);
        ui_set_text(W.k[1], buf);
    } else {
        ui_set_text(W.k[1], EL_UI_NA);
    }
    ui_set_text(W.k[2], st->net_ip[0] ? st->net_ip : EL_UI_NA);
    char host[64];
    if (s->upload_url[0] && el_uplink_url_host(s->upload_url, host, sizeof host) == EL_OK) {
        snprintf(buf, sizeof buf, "%s%s", el_uplink_url_is_https(s->upload_url) ? UI_ICON_LOCK " " : "", host);
        ui_set_text(W.k[3], buf);
    } else {
        ui_set_text(W.k[3], "Not set");
    }
    if (st->uplink_last_ok_s > 0) ui_fmt_short(st->uplink_last_ok_s, buf, sizeof buf);
    else snprintf(buf, sizeof buf, "Never");
    if (st->uplink_busy) snprintf(buf, sizeof buf, "Sending...");
    ui_set_text(W.k[4], buf);
    if (st->uplink_pending) snprintf(buf, sizeof buf, "%lu result%s", (unsigned long)st->uplink_pending,
                                     st->uplink_pending == 1 ? "" : "s");
    else snprintf(buf, sizeof buf, "None");
    ui_set_text(W.k[5], buf);

    const char *msg;
    if (ns == NET_OFF)
        msg = "Wi-Fi is off. When on, each result is sent to your server. "
              "Send-only: nothing comes in over Wi-Fi.";
    else if (s->upload_url[0] && !el_uplink_url_is_https(s->upload_url))
        msg = "Warning: the server address is http://, so results travel unencrypted. Use https:// if you can.";
    else if (st->net_error[0])
        msg = st->net_error;
    else if (!s->upload_url[0])
        msg = "Connected, but no upload server is set: nothing is sent.";
    else
        msg = "Send-only: the logger sends results out and accepts nothing in.";
    ui_set_text(W.msg, msg);
    lv_obj_set_style_text_color(W.msg, (st->net_error[0] && ns != NET_OFF) || (s->upload_url[0] && !el_uplink_url_is_https(s->upload_url))
                                           ? ui_tone_text(EL_UI_TONE_WARN) : UI_PAL.text2, 0);

    el_settings_t copy = *s;
    bool locked = !el_ui_set_writable(EL_SET_WIFI_ENABLED, ui_level());
    for (int i = 0; i < N_ROWS; i++) {
        char v[72];
        if (ROW_ID[i] == EL_SET_UPLOAD_URL) {
            snprintf(v, sizeof v, "%s", s->upload_url[0] ? (el_uplink_url_host(s->upload_url, host, sizeof host) == EL_OK ? host : "Set") : "Not set");
        } else if (ROW_ID[i] == EL_SET_WIFI_SSID) {
            snprintf(v, sizeof v, "%s", s->wifi_ssid[0] ? s->wifi_ssid : "Choose");
        } else {
            el_ui_set_value_text(&copy, ROW_ID[i], v, sizeof v);
        }
        ui_set_text(W.val[i], v);
        lv_obj_set_style_text_color(W.val[i], locked ? UI_PAL.text3 : UI_PAL.accent, 0);
    }
}

static lv_obj_t *wifi_create(void)
{
    lv_obj_t *s = ui_screen_new();
    lv_obj_t *h = ui_header(s, "Wi-Fi", true);
    W.test = ui_btn(h, UI_BTN_SECONDARY, LV_SYMBOL_UPLOAD, "Send test", 584, 4, 200, UI_TOUCH, test_cb, NULL);

    lv_coord_t top = UI_TOP + 8, hgt = UI_H - UI_TOP - 8 - UI_M;
    lv_obj_t *c = ui_card(s, UI_M, top, 372, hgt);
    ui_caption(c, 20, 14, "STATUS");
    W.chip = ui_chip(c, EL_UI_TONE_NEUTRAL, true, LV_SYMBOL_WIFI, "Off", UI_F20);
    lv_obj_set_pos(W.chip, 20, 38);
    static const char *const KEYS[6] = { "Network", "Signal", "IP address", "Server", "Last upload", "Unsent" };
    for (int i = 0; i < 6; i++) {
        W.k[i] = ui_kv(c, 20, 84 + i * 30, 332, KEYS[i], "", UI_F16);
    }
    W.msg = ui_label_at(c, UI_F14, UI_PAL.text2, 20, 270, "");
    lv_obj_set_width(W.msg, 332);
    lv_label_set_long_mode(W.msg, LV_LABEL_LONG_WRAP);

    lv_obj_t *r = ui_card(s, 400, top, UI_W - 400 - UI_M, hgt);
    ui_caption(r, 20, 14, "SETUP (ADMIN)");
    for (int i = 0; i < N_ROWS; i++) {
        lv_coord_t y = (lv_coord_t)(40 + i * ROW_H);
        lv_obj_t *row = ui_row_btn(r, 8, y, UI_W - 400 - UI_M - 16, ROW_H, row_cb, (void *)(intptr_t)i);
        ui_label_at(row, UI_F16, UI_PAL.text, 12, 13, ROW_TITLE[i]);
        W.val[i] = ui_label(row, UI_F16, UI_PAL.accent, "");
        lv_obj_set_width(W.val[i], 170);
        lv_label_set_long_mode(W.val[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(W.val[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(W.val[i], LV_ALIGN_RIGHT_MID, -36, 0);
        if (i) ui_hline(r, 20, y, UI_W - 400 - UI_M - 40);
    }
    return s;
}

static void wifi_show(const app_state_t *st)
{
    W.scan_wait = false;
    render(st);
}

static void wifi_refresh(const app_state_t *st)
{
    if (W.scan_wait) {
        int n = ui_port_wifi_scan_results(W.aps, SCAN_MAX);
        if (n >= 0) {
            W.scan_wait = false;
            W.n_aps = n;
            show_scan_list();
        }
    }
    render(st);
}

static void wifi_event(app_event_id_t id, int32_t arg, const app_state_t *st)
{
    (void)arg;
    if (id == EVT_NET_CHANGED || id == EVT_SETTINGS_CHANGED) render(st);
}

const ui_scr_ops_t UI_SCR_WIFI = { wifi_create, wifi_show, wifi_refresh, wifi_event };
