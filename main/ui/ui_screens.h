#pragma once
/* Screen registry. Each screen supplies its ops; ui.c owns creation, navigation and the 500 ms
 * refresh tick. Rules: create() builds widgets once (screens are kept, not rebuilt); show()
 * reloads data (called every time the screen is entered); refresh() updates from an app_state
 * snapshot (500 ms tick + after every event while visible) and must be cheap; event() gets the
 * events the screen cares about. Screens post requests, never call drivers. */
#include "lvgl.h"
#include "app_state.h"
#include "app_events.h"
#include "ui.h"
#include "ui_theme.h"
#include "el_stats.h"

typedef struct {
    lv_obj_t *(*create)(void);
    void      (*show)(const app_state_t *s);                 /* may be NULL */
    void      (*refresh)(const app_state_t *s);              /* may be NULL */
    void      (*event)(app_event_id_t id, int32_t arg, const app_state_t *s);   /* may be NULL */
} ui_scr_ops_t;

extern const ui_scr_ops_t UI_SCR_HOME, UI_SCR_MEASURING, UI_SCR_RESULT;          /* ui_scr_measure.c */
extern const ui_scr_ops_t UI_SCR_HISTORY, UI_SCR_TREND, UI_SCR_EXPORT;           /* ui_scr_data.c */
extern const ui_scr_ops_t UI_SCR_CALIBRATE, UI_SCR_SETTINGS, UI_SCR_SET_TIME;    /* ui_scr_setup.c */
extern const ui_scr_ops_t UI_SCR_WIFI;                                           /* ui_scr_wifi.c */
extern const ui_scr_ops_t UI_SCR_DIAG, UI_SCR_ABOUT;                             /* ui_scr_system.c */

/* Open Settings on one page (el_ui_set_group_t; EL_UI_SG_COUNT = System). */
void ui_settings_open(int group);

/* Status bar (ui_statusbar.c), on lv_layer_top() so every screen shares one. */
void ui_statusbar_create(void);
void ui_statusbar_refresh(const app_state_t *s);

/* Small shared formatters (ui_statusbar.c). */
#define UI_DEG_C "\xC2\xB0" "C"
void ui_fmt_temp(float c, char *buf, size_t cap);             /* "-18.2": decimals setting, no unit */
void ui_fmt_rh(float pct, char *buf, size_t cap);             /* "45.1": decimals setting, no unit */
void ui_fmt_limits(const el_limits_t *l, char *buf, size_t cap);   /* "5.0 - 35.0 degC . 20 - 80 %RH" */
void ui_fmt_datetime(int64_t t, char *buf, size_t cap);      /* local, user's date format */
void ui_fmt_short(int64_t t, char *buf, size_t cap);         /* "Oct 30 14:05" */
void ui_fmt_date(int64_t t, char *buf, size_t cap);          /* local date only, user's format */

/* Logo images (ui_logo.c, tools/gen_logo.py): dark text for light screens, light for dark. */
extern const lv_img_dsc_t ui_logo_light, ui_logo_dark;
const lv_img_dsc_t *ui_logo(void);                           /* the one for the current theme */
