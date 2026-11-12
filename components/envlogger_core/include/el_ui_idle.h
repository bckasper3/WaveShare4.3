#pragma once
/* Screen idle dim / off logic (saves the backlight; the logger keeps running).
 *   ON --idle_dim_s--> DIM --idle_off_s (from the last touch)--> OFF
 * Any touch returns to ON. A touch while OFF only wakes the screen and must not act on the
 * widget under the finger (el_ui_idle_activity returns true = swallow it). While the device is
 * busy (measuring, exporting) or on a screen that must stay visible, the caller
 * passes keep_awake and the timer restarts. Settings of 0 disable that stage.
 * Time is passed in (ms, wraps after 49 days: unsigned differences). Not thread-safe. */
#include <stdbool.h>
#include <stdint.h>

typedef enum { EL_UI_PWR_ON = 0, EL_UI_PWR_DIM, EL_UI_PWR_OFF } el_ui_pwr_t;

#define EL_UI_DIM_PCT 15u     /* dim level (the 4.3B backlight is on/off only: dim = on) */

typedef struct {
    uint32_t    last_ms;
    el_ui_pwr_t state;
} el_ui_idle_t;

void el_ui_idle_init(el_ui_idle_t *t, uint32_t now_ms);
/* User touched the screen. Returns true if the touch must be swallowed (screen was OFF). */
bool el_ui_idle_activity(el_ui_idle_t *t, uint32_t now_ms);
/* Advance; returns the state and sets *changed (may be NULL) when it differs from before.
 * If idle_off_s <= idle_dim_s (both > 0) the off stage still happens at idle_off_s. */
el_ui_pwr_t el_ui_idle_tick(el_ui_idle_t *t, uint32_t now_ms, uint16_t idle_dim_s,
                            uint16_t idle_off_s, bool keep_awake, bool *changed);
/* Backlight percentage for a state: ON = brightness setting (at least 10 %), DIM =
 * min(setting, EL_UI_DIM_PCT), OFF = 0. */
uint8_t el_ui_idle_backlight_pct(el_ui_pwr_t state, uint8_t brightness_pct);
