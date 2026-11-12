#pragma once
/* LVGL UI (800x480 landscape): screen manager, status bar, dialogs, login session, idle dimming.
 * All LVGL calls outside the LVGL task must hold the LVGL lock (ui_port_lock / display_lock).
 * The UI never calls drivers: it reads app_state, posts app_events, uses storage.h and ui_port.h.
 *
 * Screens (one ui_scr_*.c file per group, registry in ui_screens.h):
 *   HOME       latest reading (temperature, humidity, dew point), logging status, MEASURE
 *   MEASURING  progress ring, reading n of m, cancel
 *   RESULT     big temperature + humidity, status colour + label + icon, SD, flags; faults
 *   HISTORY    paged list (newest first), detail, delete all (Admin)
 *   TREND      temperature and humidity charts with the alarm limits
 *   EXPORT     CSV to the SD card, scope, progress
 *   CALIBRATE  temperature / humidity offsets against a reference instrument (Admin)
 *   SETTINGS   every el_settings entry by group, login/logout, links to the system screens
 *   DIAG       device status, storage, I/O, self-test
 *   ABOUT      firmware, board, serial, firmware update from the SD card
 *   SET_TIME   date/time rollers (local time; the RTC keeps UTC)
 *   WIFI       Wi-Fi status, network scan, upload server, send test (send-only, Admin to change)
 * Before HOME, a boot screen shows the logo until main.c calls ui_boot_done().
 * Navigation: Menu on the status bar opens every page from every page; screens with a header
 * also have Back and Home. `el_sim --navtest` taps through all of it.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "app_events.h"
#include "el_access.h"
#include "el_settings.h"
#include "el_ui_text.h"
#include "lvgl.h"

typedef enum {
    SCR_HOME = 0, SCR_MEASURING, SCR_RESULT, SCR_HISTORY, SCR_TREND, SCR_EXPORT,
    SCR_CALIBRATE, SCR_SETTINGS, SCR_DIAG, SCR_ABOUT, SCR_SET_TIME, SCR_WIFI, SCR_COUNT
} ui_screen_t;

esp_err_t ui_init(void);                 /* theme, status bar, boot screen. Takes the LVGL lock itself */
/* Boot finished (self-test done): leave the logo for SET_TIME (clock not set) or HOME. The logo
 * stays up at least UI_BOOT_MIN_MS. Takes the lock. */
void      ui_boot_done(void);
void      ui_show(ui_screen_t s);        /* takes the lock */
void      ui_notify(app_event_id_t id, int32_t arg);   /* controller task; takes the lock */
void      ui_toast(const char *msg, uint32_t ms);      /* ms 0 = stays until tapped; takes the lock */
void      ui_activity(void);             /* wake the screen (e.g. remote trigger) */
void      ui_screen_off(void);           /* backlight off now; the next touch only wakes. Lock held */

/* ---- for screen code (LVGL task / lock held) ---- */
void        ui_go(ui_screen_t s);        /* navigate (pushes the current screen) */
void        ui_back(void);               /* previous screen, HOME if none */
/* A run the user started owns the screen: leaving would only be undone by its next progress
 * event. True (with a toast saying why) while such a run measures, and during the boot screen.
 * Automatic (periodic) readings never block navigation. */
bool        ui_nav_blocked(void);
ui_screen_t ui_current(void);
void        ui_toast_locked(const char *msg, uint32_t ms);
const el_settings_t *ui_settings(void);  /* cached copy, refreshed on EVT_SETTINGS_CHANGED */

/* Login session of the touchscreen (el_access). */
el_level_t  ui_level(void);
bool        ui_allowed(el_action_t a);   /* el_access_allows(ui_level(), a) */
/* Allowed: true. Else toast "Log in as Admin to <what>", open the login dialog, false. */
bool        ui_require(el_action_t a, const char *what);
void        ui_login_dialog(el_level_t want);    /* OPERATOR = let the user pick */
void        ui_logout(void);

/* ---- dialogs (modal, on the top layer; one at a time) ---- */
/* Return true to close the dialog, false to keep it open (e.g. after showing an error). */
typedef bool (*ui_text_cb_t)(const char *text, void *user);
typedef void (*ui_choice_cb_t)(int index, void *user);
typedef void (*ui_confirm_cb_t)(void *user);

/* Text entry: EL_UI_CS_TEXT / ID -> keyboard; DIGITS / INT / DECIMAL -> numeric pad.
 * max_chars = field limit (EL_*_LEN - 1). masked: show dots (PIN). */
void ui_text_dialog(const char *title, const char *initial, el_ui_charset_t cs, size_t max_chars,
                    bool masked, ui_text_cb_t cb, void *user);
void ui_choice_dialog(const char *title, const char *const *options, int n, int selected,
                      ui_choice_cb_t cb, void *user);
void ui_confirm_dialog(const char *title, const char *msg, const char *ok_text, bool danger,
                       ui_confirm_cb_t cb, void *user);
/* Read-only key/value list with a Close button (n <= 10). */
void ui_info_dialog(const char *title, const char *const *keys, const char *const *vals, int n);
/* The page menu (status bar Menu button): one tile per page. Measuring has no tile: it only
 * exists during a run. */
void ui_menu_dialog(void);
int  ui_menu_count(void);
ui_screen_t ui_menu_screen(int tile);                                  /* page behind a tile */
void ui_menu_center(int tile, lv_coord_t *x, lv_coord_t *y);           /* screen position of a tile */
void ui_dialog_close(void);
bool ui_dialog_open(void);
