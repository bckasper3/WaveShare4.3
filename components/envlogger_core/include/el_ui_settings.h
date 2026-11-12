#pragma once
/* Presentation layer for the el_settings schema: page (group), title, editor kind and display
 * text of each setting. The schema stays the only source of keys, ranges, defaults, levels and
 * help; this file only adds human wording. A setting added to the schema without an entry here
 * still appears (title = its key, group by write level), so nothing silently disappears. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_access.h"
#include "el_err.h"
#include "el_settings.h"

typedef enum {
    EL_UI_SG_MEASURE = 0,   /* repeats, decimals, interval, location, limits, offsets, sensor */
    EL_UI_SG_DISPLAY,       /* brightness, idle, date format, UTC offset, theme */
    EL_UI_SG_COMMS,         /* Modbus, serial stream, DO1, DI0, Wi-Fi */
    EL_UI_SG_SECURITY,      /* admin PIN, login timeout */
    EL_UI_SG_OPERATOR,      /* operator ID */
    EL_UI_SG_DEVICE,        /* serial number, engineer code (ENGINEER) */
    EL_UI_SG_COUNT
} el_ui_set_group_t;

typedef enum {
    EL_UI_ED_TOGGLE = 0,    /* bool: switch */
    EL_UI_ED_CHOICE,        /* enum: list of options */
    EL_UI_ED_INT,           /* numeric pad, integer */
    EL_UI_ED_FLOAT,         /* numeric pad, decimal */
    EL_UI_ED_TEXT,          /* keyboard */
    EL_UI_ED_PIN,           /* numeric pad, masked */
    EL_UI_ED_SECRET         /* keyboard, masked (Wi-Fi password, access keys) */
} el_ui_editor_t;

const char       *el_ui_set_group_name(el_ui_set_group_t g);   /* "Measurement", ... */
el_ui_set_group_t el_ui_set_group(el_set_id_t id);
const char       *el_ui_set_title(el_set_id_t id);             /* "Readings per result"; key if unknown */
el_ui_editor_t    el_ui_set_editor(el_set_id_t id);
/* Number of settings in group g and the n-th one (display order = schema order). */
size_t            el_ui_set_group_count(el_ui_set_group_t g);
el_set_id_t       el_ui_set_group_item(el_ui_set_group_t g, size_t n);   /* EL_SET_COUNT if none */

/* Human text for enum option idx ("ON_ALARM" -> "On while in alarm"); the schema name for unknown
 * ones; NULL if idx is out of range. */
const char *el_ui_set_enum_text(el_set_id_t id, uint8_t idx);

/* Value as shown in a settings row: bool "On"/"Off"; enum human text; numbers with the
 * schema's unit ("120 s", "35 \u00b0C"); a value v shown as "Off"/"Never" when the help says
 * "<v> = off"/"<v> = never"; secret: "Set" / "Not set" (never the value); empty string "(none)".
 * EL_ERR_ARG (NULL / unknown id), EL_ERR_SIZE (truncated). */
el_err_t el_ui_set_value_text(const el_settings_t *s, el_set_id_t id, char *buf, size_t cap);

/* May `level` edit this setting (el_set_def_t.write_level)? */
bool el_ui_set_writable(el_set_id_t id, el_level_t level);
