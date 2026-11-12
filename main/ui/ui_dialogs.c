/* Modal dialogs on the top layer: text keyboard, numeric pad, choice list, confirm, page menu.
 * One dialog at a time; opening a new one closes the old one. */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "ui_theme.h"
#include "storage.h"
#include "app_state.h"

static lv_obj_t *s_modal;            /* dimmed full-screen backdrop; the dialog is its child */
static lv_obj_t *s_ta;
static ui_text_cb_t   s_text_cb;
static ui_choice_cb_t s_choice_cb;
static ui_confirm_cb_t s_confirm_cb;
static void *s_user;
static char  s_accept[100];

void ui_dialog_close(void)
{
    if (s_modal) lv_obj_del(s_modal);
    s_modal = NULL;
    s_ta = NULL;
}

bool ui_dialog_open(void) { return s_modal != NULL; }

static lv_obj_t *backdrop(void)
{
    ui_dialog_close();
    s_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_modal);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_CLICKABLE);       /* swallow touches behind the dialog */
    lv_obj_set_size(s_modal, UI_W, UI_H);
    lv_obj_set_style_bg_color(s_modal, UI_PAL.overlay, 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_50, 0);
    return s_modal;
}

static lv_obj_t *panel(lv_coord_t w, lv_coord_t h, const char *title)
{
    lv_obj_t *p = ui_card(backdrop(), (UI_W - w) / 2, (UI_H - h) / 2, w, h);
    lv_obj_set_style_shadow_width(p, 32, 0);
    lv_obj_set_style_shadow_opa(p, LV_OPA_30, 0);
    if (title) ui_label_at(p, UI_F24, UI_PAL.text, 24, 18, title);
    return p;
}

static void close_cb(lv_event_t *e) { (void)e; ui_dialog_close(); }

/* ---------------- text / numeric entry ---------------- */

static void kb_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CANCEL) { ui_dialog_close(); return; }
    if (code != LV_EVENT_READY || !s_ta) return;
    char text[160];                    /* longest entry: the 120-character Wi-Fi upload server */
    snprintf(text, sizeof text, "%s", lv_textarea_get_text(s_ta));
    ui_text_cb_t cb = s_text_cb;
    void *user = s_user;
    if (!cb || cb(text, user)) {
        if (s_text_cb == cb && s_user == user) ui_dialog_close();   /* cb may have opened a new dialog */
    } else if (s_ta) {
        lv_textarea_set_text(s_ta, "");
    }
}

static const char *PAD_MAP[] = { "1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n",
                                       "-", "0", ".", "\n", LV_SYMBOL_CLOSE, LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, "" };
#define K LV_BTNMATRIX_CTRL_NO_REPEAT
#define H (LV_BTNMATRIX_CTRL_HIDDEN | LV_BTNMATRIX_CTRL_DISABLED)
static const lv_btnmatrix_ctrl_t PAD_DIGITS[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 | H, 1, 1 | H, 1 | K, 1, 1 | K };
static const lv_btnmatrix_ctrl_t PAD_INT[]    = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     1, 1 | H, 1 | K, 1, 1 | K };
static const lv_btnmatrix_ctrl_t PAD_DEC[]    = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     1, 1,     1 | K, 1, 1 | K };
#undef K
#undef H

void ui_text_dialog(const char *title, const char *initial, el_ui_charset_t cs, size_t max_chars,
                    bool masked, ui_text_cb_t cb, void *user)
{
    bool numeric = cs == EL_UI_CS_DIGITS || cs == EL_UI_CS_INT || cs == EL_UI_CS_DECIMAL;
    size_t n = 0;
    for (int c = 0x20; c <= 0x7E && n < sizeof s_accept - 1; c++)
        if (el_ui_text_char_ok((char)c, cs)) s_accept[n++] = (char)c;
    s_accept[n] = '\0';

    lv_obj_t *p;
    lv_obj_t *kb;
    if (numeric) {
        p = panel(440, 452, title);
        s_ta = lv_textarea_create(p);
        lv_obj_set_pos(s_ta, 24, 60);
        lv_obj_set_size(s_ta, 392, 64);
        kb = lv_keyboard_create(p);
        lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, PAD_MAP,
                            cs == EL_UI_CS_DIGITS ? PAD_DIGITS : cs == EL_UI_CS_INT ? PAD_INT : PAD_DEC);
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
        lv_obj_align(kb, LV_ALIGN_TOP_LEFT, 16, 136);   /* keyboards default to BOTTOM_MID */
        lv_obj_set_size(kb, 408, 300);
    } else {
        p = ui_card(backdrop(), 0, UI_SB_H, UI_W, UI_H - UI_SB_H);
        lv_obj_set_style_radius(p, 0, 0);
        ui_label_at(p, UI_F24, UI_PAL.text, 24, 14, title);
        s_ta = lv_textarea_create(p);
        lv_obj_set_pos(s_ta, 24, 52);
        lv_obj_set_size(s_ta, UI_W - 48, 64);
        kb = lv_keyboard_create(p);
        lv_obj_align(kb, LV_ALIGN_TOP_LEFT, 0, 128);
        lv_obj_set_size(kb, UI_W, UI_H - UI_SB_H - 128);
    }
    lv_obj_set_style_text_font(s_ta, UI_F32, 0);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_accepted_chars(s_ta, s_accept);
    lv_textarea_set_max_length(s_ta, (uint32_t)max_chars);
    lv_textarea_set_password_mode(s_ta, masked);
    lv_textarea_set_text(s_ta, initial ? initial : "");
    lv_obj_add_state(s_ta, LV_STATE_FOCUSED);
    lv_obj_set_style_text_font(kb, UI_F24, 0);
    lv_keyboard_set_textarea(kb, s_ta);
    lv_obj_add_event_cb(kb, kb_event, LV_EVENT_ALL, NULL);
    s_text_cb = cb;
    s_user = user;
}

/* ---------------- choice ---------------- */

static void choice_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ui_choice_cb_t cb = s_choice_cb;
    void *user = s_user;
    ui_dialog_close();
    if (cb) cb(idx, user);
}

void ui_choice_dialog(const char *title, const char *const *options, int n, int selected,
                      ui_choice_cb_t cb, void *user)
{
    const lv_coord_t row = 64;
    int vis = n < 5 ? n : 5;
    lv_coord_t h = 72 + vis * (row + 8) + 88;
    lv_obj_t *p = panel(480, h, title);
    lv_obj_t *list = ui_box(p, 16, 64, 448, vis * (row + 8));
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = ui_btn(list, i == selected ? UI_BTN_SECONDARY : UI_BTN_GHOST,
                             i == selected ? LV_SYMBOL_OK : NULL, options[i], 0, i * (row + 8), 448 - 8, row,
                             choice_cb, (void *)(intptr_t)i);
        lv_obj_t *l = lv_obj_get_child(b, 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 16, 0);
        if (i != selected) lv_obj_set_style_bg_color(b, UI_PAL.surface2, 0), lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    }
    ui_btn(p, UI_BTN_GHOST, NULL, "Cancel", 480 - 176, h - 80, 160, UI_TOUCH, close_cb, NULL);
    s_choice_cb = cb;
    s_user = user;
}

/* ---------------- confirm ---------------- */

static void confirm_ok(lv_event_t *e)
{
    (void)e;
    ui_confirm_cb_t cb = s_confirm_cb;
    void *user = s_user;
    ui_dialog_close();
    if (cb) cb(user);
}

void ui_confirm_dialog(const char *title, const char *msg, const char *ok_text, bool danger,
                       ui_confirm_cb_t cb, void *user)
{
    lv_point_t sz;
    lv_txt_get_size(&sz, msg, UI_F20, 0, 0, 512, LV_TEXT_FLAG_NONE);
    lv_coord_t h = 64 + sz.y + 24 + UI_TOUCH + 24;
    lv_obj_t *p = panel(560, h, title);
    lv_obj_t *m = ui_label_at(p, UI_F20, UI_PAL.text2, 24, 64, msg);
    lv_obj_set_width(m, 512);
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    ui_btn(p, UI_BTN_GHOST, NULL, "Cancel", 560 - 24 - 200 - 12 - 160, h - UI_TOUCH - 24, 160, UI_TOUCH, close_cb, NULL);
    ui_btn(p, danger ? UI_BTN_DANGER_FILL : UI_BTN_PRIMARY, NULL, ok_text ? ok_text : "OK",
           560 - 24 - 200, h - UI_TOUCH - 24, 200, UI_TOUCH, confirm_ok, NULL);
    s_confirm_cb = cb;
    s_user = user;
}

/* ---------------- page menu ---------------- */

#define MENU_W      768
#define MENU_H      456
#define MENU_COLS   5
#define MENU_TILE_W 136
#define MENU_TILE_H 116
#define MENU_GAP    14
#define MENU_TOP    60

static const struct { ui_screen_t scr; const char *icon, *text; } MENU[] = {
    { SCR_HOME,      UI_ICON_TH_LARGE,     "Home" },
    { SCR_RESULT,    UI_ICON_THERMO,       "Last reading" },
    { SCR_HISTORY,   UI_ICON_HISTORY,      "History" },
    { SCR_TREND,     UI_ICON_CHART_LINE,   "Trend" },
    { SCR_EXPORT,    UI_ICON_HDD,          "Export" },
    { SCR_CALIBRATE, UI_ICON_BALANCE,      "Calibrate" },
    { SCR_SETTINGS,  UI_ICON_SLIDERS,      "Settings" },
    { SCR_SET_TIME,  UI_ICON_CALENDAR,     "Date & time" },
    { SCR_WIFI,      LV_SYMBOL_WIFI,       "Wi-Fi" },
    { SCR_DIAG,      UI_ICON_STETHOSCOPE,  "Diagnostics" },
    { SCR_ABOUT,     UI_ICON_INFO_CIRCLE,  "About" },
};
#define MENU_N ((int)(sizeof MENU / sizeof MENU[0]))

int ui_menu_count(void) { return MENU_N; }
ui_screen_t ui_menu_screen(int tile) { return MENU[tile].scr; }

static void menu_tile_pos(int tile, lv_coord_t *x, lv_coord_t *y)
{
    *x = (lv_coord_t)(16 + (tile % MENU_COLS) * (MENU_TILE_W + MENU_GAP));
    *y = (lv_coord_t)(MENU_TOP + (tile / MENU_COLS) * (MENU_TILE_H + MENU_GAP));
}

void ui_menu_center(int tile, lv_coord_t *x, lv_coord_t *y)
{
    menu_tile_pos(tile, x, y);
    *x = (lv_coord_t)(*x + (UI_W - MENU_W) / 2 + MENU_TILE_W / 2);
    *y = (lv_coord_t)(*y + (UI_H - MENU_H) / 2 + MENU_TILE_H / 2);
}

static void menu_cb(lv_event_t *e)
{
    ui_screen_t to = (ui_screen_t)(intptr_t)lv_event_get_user_data(e);
    ui_dialog_close();
    ui_go(to);                         /* the current page's tile just closes the menu */
}

void ui_menu_dialog(void)
{
    app_state_t st;
    app_state_get(&st);
    lv_obj_t *p = panel(MENU_W, MENU_H, "Go to");
    ui_btn(p, UI_BTN_GHOST, LV_SYMBOL_CLOSE, "Close", MENU_W - 16 - MENU_TILE_W, 4, MENU_TILE_W, 52, close_cb, NULL);
    for (int i = 0; i < MENU_N; i++) {
        lv_coord_t x, y;
        menu_tile_pos(i, &x, &y);
        lv_obj_t *b = ui_btn(p, UI_BTN_SECONDARY, NULL, NULL, x, y, MENU_TILE_W, MENU_TILE_H, menu_cb,
                             (void *)(intptr_t)MENU[i].scr);
        bool here = MENU[i].scr == ui_current();
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, here ? UI_PAL.accent : UI_PAL.border, 0);
        if (here) {
            lv_obj_set_style_bg_color(b, UI_PAL.accent_tint, 0);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        }
        lv_obj_t *ic = ui_label(b, UI_F32, UI_PAL.accent, MENU[i].icon);
        lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 18);
        lv_obj_t *t = ui_label(b, UI_F16, UI_PAL.text, MENU[i].text);
        lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -14);
        if (MENU[i].scr == SCR_RESULT && !st.has_result) ui_btn_enable(b, false);   /* nothing to show yet */
    }
}

/* ---------------- info ---------------- */

void ui_info_dialog(const char *title, const char *const *keys, const char *const *vals, int n)
{
    if (n > 10) n = 10;
    lv_coord_t h = 72 + n * 36 + 96;
    lv_obj_t *p = panel(560, h, title);
    for (int i = 0; i < n; i++) ui_kv(p, 24, 64 + i * 36, 512, keys[i], vals[i], NULL);
    ui_btn(p, UI_BTN_PRIMARY, NULL, "Close", 560 - 24 - 180, h - 84, 180, UI_TOUCH, close_cb, NULL);
}
