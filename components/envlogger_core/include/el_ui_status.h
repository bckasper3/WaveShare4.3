#pragma once
/* Status presentation for the UI: which tone (colour family), label and icon a result, a
 * fault or a badge gets. Colours are 0xRRGGBB for the light and dark themes.
 * Rule: status is never shown by colour alone; every tone comes with a label and a glyph.
 * Pure C, no state. */
#include <stdbool.h>
#include <stdint.h>
#include "el_access.h"
#include "el_meas.h"
#include "el_result.h"

typedef enum {
    EL_UI_TONE_NEUTRAL = 0,   /* grey: no result, disabled */
    EL_UI_TONE_OK,            /* green: normal */
    EL_UI_TONE_WARN,          /* amber: warnings (log nearly full, ...) */
    EL_UI_TONE_BAD,           /* red: alarm */
    EL_UI_TONE_FAULT,         /* purple: error / fault / engineer mode */
    EL_UI_TONE_ACCENT,        /* teal accent: info, admin */
    EL_UI_TONE_COUNT
} el_ui_tone_t;

typedef enum {
    EL_UI_GLYPH_NONE = 0,
    EL_UI_GLYPH_CHECK,        /* check in a circle */
    EL_UI_GLYPH_WARN,         /* triangle with ! */
    EL_UI_GLYPH_ALARM,        /* cross in a circle */
    EL_UI_GLYPH_FAULT,        /* ! in a circle */
    EL_UI_GLYPH_INFO,
    EL_UI_GLYPH_COUNT
} el_ui_glyph_t;

typedef struct {
    uint32_t fg;        /* strong colour: icons, borders, filled chips */
    uint32_t tint;      /* light background for a panel in this tone */
    uint32_t on_fg;     /* text colour on top of fg (contrast >= 4.5:1) */
    uint32_t text;      /* text colour in this tone on the normal background */
} el_ui_tone_colors_t;

/* Accent = teal, chosen dark enough for white text (AA). */
void el_ui_tone_colors(el_ui_tone_t tone, bool dark, el_ui_tone_colors_t *out);

el_ui_tone_t  el_ui_status_tone(el_result_status_t s);
el_ui_glyph_t el_ui_status_glyph(el_result_status_t s);
const char   *el_ui_status_label(el_result_status_t s);   /* "NORMAL", "ALARM", "ERROR", "NO RESULT" */

/* Plain-language fault text and what to do about it (Result screen, fault state). */
const char *el_ui_fault_title(el_meas_fault_t f);
const char *el_ui_fault_hint(el_meas_fault_t f);
/* Why a measurement request was refused (toast). */
const char *el_ui_reject_text(el_meas_reject_t r);

/* ---- badges (status bar) ---- */
#define EL_UI_BADGE_LEN 16
typedef struct {
    bool         show;
    el_ui_tone_t tone;
    char         text[EL_UI_BADGE_LEN];
} el_ui_badge_t;

/* Login level: OPERATOR (NEUTRAL), ADMIN (ACCENT), ENGINEER (FAULT). Always shown. */
void el_ui_level_badge(el_level_t level, el_ui_badge_t *out);

/* Short alarm text for the screen: "Temp. high", "Humidity low", "Temp. and RH" (both
 * quantities), "" for none. */
const char *el_ui_alarm_text(uint8_t alarms);
