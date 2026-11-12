/* Visual system (ui_theme.h): palettes, the LVGL default theme, widget helpers. */
#include <stdio.h>
#include <string.h>
#include "ui_theme.h"
#include "ui.h"

ui_palette_t UI_PAL;

static lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

static void set_palette(bool dark)
{
    el_ui_tone_colors_t acc;
    el_ui_tone_colors(EL_UI_TONE_ACCENT, dark, &acc);
    UI_PAL.dark = dark;
    if (!dark) {
        UI_PAL.bg = hex(0xF3F5F8);   UI_PAL.surface = hex(0xFFFFFF); UI_PAL.surface2 = hex(0xEDF0F4);
        UI_PAL.border = hex(0xD5DBE3);
        UI_PAL.text = hex(0x17242B); UI_PAL.text2 = hex(0x4A5A64); UI_PAL.text3 = hex(0x76858F);
        UI_PAL.accent_pressed = hex(0x095F61);
        UI_PAL.bar_bg = hex(0x1F2A30);           /* neutral dark slate */ UI_PAL.bar_text = hex(0xFFFFFF); UI_PAL.bar_text2 = hex(0xA9B4C0);
        UI_PAL.overlay = hex(0x000000);
    } else {
        UI_PAL.bg = hex(0x0E1318);   UI_PAL.surface = hex(0x18202A); UI_PAL.surface2 = hex(0x222C38);
        UI_PAL.border = hex(0x2E3946);
        UI_PAL.text = hex(0xE8EDF2); UI_PAL.text2 = hex(0xB5C0CB); UI_PAL.text3 = hex(0x7F8B98);
        UI_PAL.accent_pressed = hex(0x22948F);
        UI_PAL.bar_bg = hex(0x151D22); UI_PAL.bar_text = hex(0xE8EDF2); UI_PAL.bar_text2 = hex(0x8A96A3);
        UI_PAL.overlay = hex(0x000000);
    }
    UI_PAL.accent = hex(acc.fg);
    UI_PAL.accent_tint = hex(acc.tint);
    UI_PAL.on_accent = hex(acc.on_fg);
}

void ui_theme_init(bool dark)
{
    set_palette(dark);
    lv_disp_t *d = lv_disp_get_default();
    lv_theme_t *th = lv_theme_default_init(d, UI_PAL.accent, ui_tone_fg(EL_UI_TONE_BAD), dark, UI_F20);
    lv_disp_set_theme(d, th);
}

bool ui_theme_is_dark(void) { return UI_PAL.dark; }

static el_ui_tone_colors_t tone(el_ui_tone_t t)
{
    el_ui_tone_colors_t c;
    el_ui_tone_colors(t, UI_PAL.dark, &c);
    return c;
}
lv_color_t ui_tone_fg(el_ui_tone_t t)   { return hex(tone(t).fg); }
lv_color_t ui_tone_tint(el_ui_tone_t t) { return hex(tone(t).tint); }
lv_color_t ui_tone_on(el_ui_tone_t t)   { return hex(tone(t).on_fg); }
lv_color_t ui_tone_text(el_ui_tone_t t) { return hex(tone(t).text); }

const char *ui_glyph(el_ui_glyph_t g)
{
    switch (g) {
    case EL_UI_GLYPH_CHECK: return UI_ICON_CHECK_CIRCLE;
    case EL_UI_GLYPH_WARN:  return LV_SYMBOL_WARNING;
    case EL_UI_GLYPH_ALARM: return UI_ICON_TIMES_CIRCLE;
    case EL_UI_GLYPH_FAULT: return UI_ICON_EXCL_CIRCLE;
    case EL_UI_GLYPH_INFO:  return UI_ICON_INFO_CIRCLE;
    default:                return "";
    }
}

/* ---------------- widgets ---------------- */

static void plain(lv_obj_t *o)
{
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *ui_screen_new(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s, UI_PAL.bg, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s, 0, 0);
    return s;
}

static void back_cb(lv_event_t *e) { (void)e; if (!ui_nav_blocked()) ui_back(); }
static void home_cb(lv_event_t *e) { (void)e; if (!ui_nav_blocked()) ui_go(SCR_HOME); }

lv_obj_t *ui_header(lv_obj_t *scr, const char *title, bool back)
{
    lv_obj_t *h = lv_obj_create(scr);
    plain(h);
    lv_obj_set_pos(h, 0, UI_SB_H);
    lv_obj_set_size(h, UI_W, UI_HDR_H);
    lv_obj_set_style_bg_color(h, UI_PAL.surface, 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(h, 1, 0);
    lv_obj_set_style_border_color(h, UI_PAL.border, 0);
    lv_coord_t x = UI_M + 8;
    if (back) {
        ui_btn(h, UI_BTN_GHOST, UI_ICON_ARROW_LEFT, NULL, 8, (UI_HDR_H - UI_TOUCH) / 2, 72, UI_TOUCH, back_cb, NULL);
        ui_btn(h, UI_BTN_GHOST, UI_ICON_TH_LARGE, NULL, 84, (UI_HDR_H - UI_TOUCH) / 2, 72, UI_TOUCH, home_cb, NULL);
        x = 164;
    }
    lv_obj_t *t = ui_label(h, UI_F32, UI_PAL.text, title);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, x, 0);
    return h;
}

lv_obj_t *ui_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *c = lv_obj_create(parent);
    plain(c);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, UI_PAL.surface, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, UI_R, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, UI_PAL.border, 0);
    return c;
}

lv_obj_t *ui_box(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *c = lv_obj_create(parent);
    plain(c);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    return c;
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *f, lv_color_t c, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, text ? text : "");
    return l;
}

lv_obj_t *ui_label_at(lv_obj_t *parent, const lv_font_t *f, lv_color_t c, lv_coord_t x, lv_coord_t y,
                      const char *text)
{
    lv_obj_t *l = ui_label(parent, f, c, text);
    lv_obj_set_pos(l, x, y);
    return l;
}

lv_obj_t *ui_caption(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, const char *text)
{
    lv_obj_t *l = ui_label_at(parent, UI_F14, UI_PAL.text3, x, y, text);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    return l;
}

static void btn_style(lv_obj_t *b, ui_btn_kind_t kind)
{
    lv_color_t bg = UI_PAL.surface, fg = UI_PAL.text, pr = UI_PAL.surface2, bd = UI_PAL.border;
    lv_opa_t bg_opa = LV_OPA_COVER;
    lv_coord_t bw = 1;
    switch (kind) {
    case UI_BTN_PRIMARY:     bg = UI_PAL.accent; fg = UI_PAL.on_accent; pr = UI_PAL.accent_pressed; bw = 0; break;
    case UI_BTN_SECONDARY:   bd = UI_PAL.accent; fg = UI_PAL.accent; pr = UI_PAL.accent_tint; bw = 2; break;
    case UI_BTN_GHOST:       bg_opa = LV_OPA_TRANSP; bw = 0; break;
    case UI_BTN_DANGER:      bd = ui_tone_fg(EL_UI_TONE_BAD); fg = ui_tone_text(EL_UI_TONE_BAD);
                             pr = ui_tone_tint(EL_UI_TONE_BAD); bw = 2; break;
    case UI_BTN_DANGER_FILL: bg = ui_tone_fg(EL_UI_TONE_BAD); fg = ui_tone_on(EL_UI_TONE_BAD);
                             pr = ui_tone_text(EL_UI_TONE_BAD); bw = 0; break;
    }
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, bg_opa, 0);
    lv_obj_set_style_bg_color(b, pr, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, bw, 0);
    lv_obj_set_style_border_color(b, bd, 0);
    lv_obj_set_style_text_color(b, fg, 0);
    lv_obj_set_style_radius(b, UI_RB, 0);
    lv_obj_set_style_opa(b, LV_OPA_40, LV_STATE_DISABLED);
}

void ui_btn_set_text(lv_obj_t *btn, const char *icon, const char *text)
{
    lv_obj_t *l = lv_obj_get_child(btn, 0);
    if (!l) return;
    char buf[64];
    if (icon && icon[0] && text && text[0]) snprintf(buf, sizeof buf, "%s  %s", icon, text);
    else snprintf(buf, sizeof buf, "%s", (icon && icon[0]) ? icon : (text ? text : ""));
    ui_set_text(l, buf);
}

lv_obj_t *ui_btn(lv_obj_t *parent, ui_btn_kind_t kind, const char *icon, const char *text,
                 lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_obj_create(parent);
    plain(b);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    btn_style(b, kind);
    const lv_font_t *f = h >= 110 ? UI_F48 : h >= 60 ? UI_F24 : UI_F20;
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_center(l);
    ui_btn_set_text(b, icon, text);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

void ui_btn_enable(lv_obj_t *btn, bool en)
{
    if (en) lv_obj_clear_state(btn, LV_STATE_DISABLED);
    else lv_obj_add_state(btn, LV_STATE_DISABLED);
}

void ui_chip_set(lv_obj_t *chip, el_ui_tone_t t, bool filled, const char *icon, const char *text)
{
    lv_obj_set_style_bg_color(chip, filled ? ui_tone_fg(t) : ui_tone_tint(t), 0);
    lv_obj_t *l = lv_obj_get_child(chip, 0);
    lv_obj_set_style_text_color(l, filled ? ui_tone_on(t) : ui_tone_text(t), 0);
    char buf[48];
    if (icon && icon[0]) snprintf(buf, sizeof buf, "%s %s", icon, text ? text : "");
    else snprintf(buf, sizeof buf, "%s", text ? text : "");
    ui_set_text(l, buf);
}

lv_obj_t *ui_chip(lv_obj_t *parent, el_ui_tone_t t, bool filled, const char *icon, const char *text,
                  const lv_font_t *f)
{
    lv_obj_t *c = lv_obj_create(parent);
    plain(c);
    lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(c, f->line_height / 2 + 2, 0);
    lv_obj_set_style_pad_ver(c, f->line_height / 6 + 1, 0);
    lv_obj_t *l = lv_label_create(c);
    lv_obj_set_style_text_font(l, f, 0);
    ui_chip_set(c, t, filled, icon, text);
    return c;
}

lv_obj_t *ui_kv(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, const char *key,
                const char *value, const lv_font_t *vf)
{
    lv_obj_t *k = ui_label_at(parent, UI_F20, UI_PAL.text2, x, y, key);
    lv_obj_set_size(k, w * 45 / 100, UI_F20->line_height);   /* fixed height: one line, "..." when long */
    lv_label_set_long_mode(k, LV_LABEL_LONG_DOT);
    lv_obj_t *v = ui_label(parent, vf ? vf : UI_F20, UI_PAL.text, value);
    lv_obj_set_size(v, w * 58 / 100, (vf ? vf : UI_F20)->line_height);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_coord_t dy = (vf && vf->line_height > UI_F20->line_height) ? (UI_F20->line_height - vf->line_height) : 0;
    lv_obj_set_pos(v, x + w - w * 58 / 100, y + dy);
    return v;
}

lv_obj_t *ui_hline(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w)
{
    lv_obj_t *l = lv_obj_create(parent);
    plain(l);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, 1);
    lv_obj_set_style_bg_color(l, UI_PAL.border, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    return l;
}

lv_obj_t *ui_row_btn(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                     lv_event_cb_t cb, void *user)
{
    lv_obj_t *r = lv_obj_create(parent);
    plain(r);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_size(r, w, h);
    lv_obj_set_style_radius(r, 8, 0);
    lv_obj_set_style_bg_color(r, UI_PAL.surface2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_t *ch = ui_label(r, UI_F20, UI_PAL.text3, LV_SYMBOL_RIGHT);
    lv_obj_align(ch, LV_ALIGN_RIGHT_MID, -12, 0);
    if (cb) lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, user);
    return r;
}

static void lock_login_cb(lv_event_t *e) { (void)e; ui_login_dialog(EL_LEVEL_ADMIN); }

lv_obj_t *ui_lock_notice(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                         const char *what)
{
    lv_obj_t *b = ui_box(parent, x, y, w, h);
    lv_obj_t *ic = ui_label(b, UI_F48, UI_PAL.text3, UI_ICON_LOCK);
    lv_obj_align(ic, LV_ALIGN_CENTER, 0, -70);
    char buf[96];
    snprintf(buf, sizeof buf, "Log in as Admin to %s", what ? what : "change this");
    lv_obj_t *t = ui_label(b, UI_F24, UI_PAL.text2, buf);
    lv_obj_set_width(t, w - 32);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -10);
    ui_btn(b, UI_BTN_PRIMARY, UI_ICON_SIGN_IN, "Log in", (w - 220) / 2, h / 2 + 30, 220, UI_TOUCH,
           lock_login_cb, NULL);
    return b;
}

void ui_set_text(lv_obj_t *label, const char *text)
{
    if (!label) return;
    const char *cur = lv_label_get_text(label);
    if (!text) text = "";
    if (cur && strcmp(cur, text) == 0) return;
    lv_label_set_text(label, text);
}
