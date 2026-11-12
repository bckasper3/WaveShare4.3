#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "el_ui_settings.h"
#include "el_ui_fmt.h"
#include "el_util.h"

/* Wording per schema key. Keyed by the key string (not the enum) so a reordered schema keeps
 * its titles; a key missing here still shows, with its key as the title. */
typedef struct {
    const char        *key;
    const char        *title;
    el_ui_set_group_t  group;
} ui_set_t;

static const ui_set_t UI_SET[] = {
    { "device_serial",        "Serial number",           EL_UI_SG_DEVICE },
    { "engineer_code",        "Engineer code",           EL_UI_SG_DEVICE },
    { "repeats",              "Readings per result",     EL_UI_SG_MEASURE },
    { "decimals",             "Decimal places",          EL_UI_SG_MEASURE },
    { "log_interval_s",       "Logging interval",        EL_UI_SG_MEASURE },
    { "location",             "Location name",           EL_UI_SG_MEASURE },
    { "temp_low_c",           "Low temperature alarm",   EL_UI_SG_MEASURE },
    { "temp_high_c",          "High temperature alarm",  EL_UI_SG_MEASURE },
    { "rh_low_pct",           "Low humidity alarm",      EL_UI_SG_MEASURE },
    { "rh_high_pct",          "High humidity alarm",     EL_UI_SG_MEASURE },
    { "temp_offset_c",        "Temperature offset",      EL_UI_SG_MEASURE },
    { "rh_offset_pct",        "Humidity offset",         EL_UI_SG_MEASURE },
    { "sensor_type",          "Sensor type",             EL_UI_SG_MEASURE },
    { "sensor_addr",          "Sensor I2C address",      EL_UI_SG_MEASURE },
    { "brightness_pct",       "Brightness",              EL_UI_SG_DISPLAY },
    { "idle_dim_s",           "Dim screen after",        EL_UI_SG_DISPLAY },
    { "idle_off_s",           "Screen off after",        EL_UI_SG_DISPLAY },
    { "date_format",          "Date format",             EL_UI_SG_DISPLAY },
    { "utc_offset_min",       "Time zone (UTC offset)",  EL_UI_SG_DISPLAY },
    { "mb_addr",              "Modbus address",          EL_UI_SG_COMMS },
    { "mb_baud",              "Modbus baud rate",        EL_UI_SG_COMMS },
    { "mb_parity",            "Modbus parity",           EL_UI_SG_COMMS },
    { "serial_stream",        "Stream results on USB",   EL_UI_SG_COMMS },
    { "do1_mode",             "Alarm output DO1",        EL_UI_SG_COMMS },
    { "di0_trigger",          "Remote start on DI0",     EL_UI_SG_COMMS },
    { "wifi_enabled",         "Wi-Fi",                   EL_UI_SG_COMMS },
    { "wifi_ssid",            "Wi-Fi network",           EL_UI_SG_COMMS },
    { "wifi_password",        "Wi-Fi password",          EL_UI_SG_COMMS },
    { "upload_url",           "Upload server",           EL_UI_SG_COMMS },
    { "upload_key",           "Server access key",       EL_UI_SG_COMMS },
    { "wifi_time_sync",       "Clock from internet",     EL_UI_SG_COMMS },
    { "admin_pin",            "Admin PIN",               EL_UI_SG_SECURITY },
    { "login_idle_s",         "Auto logout after",       EL_UI_SG_SECURITY },
    { "operator_id",          "Operator ID",             EL_UI_SG_OPERATOR },
    { "theme",                "Colour theme",            EL_UI_SG_DISPLAY },
    { "home_return_s",        "Back to Home after",      EL_UI_SG_DISPLAY },
};
#define UI_SET_N (sizeof UI_SET / sizeof UI_SET[0])

typedef struct { const char *key, *name, *text; } ui_enum_t;
static const ui_enum_t UI_ENUM[] = {
    { "date_format", "ISO", "ISO  2026-10-30" },
    { "date_format", "US", "US  10/30/2026" },
    { "date_format", "EU", "EU  30.10.2026" },
    { "mb_parity", "NONE", "None (8N1)" },
    { "mb_parity", "EVEN", "Even (8E1)" },
    { "mb_parity", "ODD", "Odd (8O1)" },
    { "do1_mode", "OFF", "Off" },
    { "do1_mode", "ON_ALARM", "On while in alarm" },
    { "do1_mode", "BUSY", "On while measuring" },
    { "theme", "LIGHT", "Light" },
    { "theme", "DARK", "Dark" },
    { "sensor_type", "SHT4X", "SHT4x (SHT40/41/45)" },
    { "sensor_type", "SHT3X", "SHT3x (SHT30/31/35)" },
    { "sensor_addr", "0X44", "0x44 (default)" },
    { "sensor_addr", "0X45", "0x45" },
};
#define UI_ENUM_N (sizeof UI_ENUM / sizeof UI_ENUM[0])

static const ui_set_t *lookup(el_set_id_t id)
{
    const el_set_def_t *d = el_settings_def(id);
    if (!d) return NULL;
    for (size_t i = 0; i < UI_SET_N; i++)
        if (!strcmp(UI_SET[i].key, d->key)) return &UI_SET[i];
    return NULL;
}

const char *el_ui_set_group_name(el_ui_set_group_t g)
{
    static const char *const N[EL_UI_SG_COUNT] = {
        "Logging & alarms", "Display", "Communication", "Security", "Operator", "Device" };
    return (unsigned)g < EL_UI_SG_COUNT ? N[g] : "Other";
}

el_ui_set_group_t el_ui_set_group(el_set_id_t id)
{
    const ui_set_t *u = lookup(id);
    if (u) return u->group;
    const el_set_def_t *d = el_settings_def(id);
    if (!d) return EL_UI_SG_MEASURE;
    /* Unknown (new) setting: place it by who may write it. */
    if (d->write_level >= EL_LEVEL_ENGINEER) return EL_UI_SG_DEVICE;
    if (d->write_level == EL_LEVEL_OPERATOR) return EL_UI_SG_OPERATOR;
    return EL_UI_SG_MEASURE;
}

const char *el_ui_set_title(el_set_id_t id)
{
    const ui_set_t *u = lookup(id);
    if (u) return u->title;
    const el_set_def_t *d = el_settings_def(id);
    return d ? d->key : "?";
}

el_ui_editor_t el_ui_set_editor(el_set_id_t id)
{
    const el_set_def_t *d = el_settings_def(id);
    if (!d) return EL_UI_ED_TEXT;
    switch (d->type) {
    case EL_SET_T_BOOL:  return EL_UI_ED_TOGGLE;
    case EL_SET_T_ENUM:  return EL_UI_ED_CHOICE;
    case EL_SET_T_INT:   return EL_UI_ED_INT;
    case EL_SET_T_FLOAT: return EL_UI_ED_FLOAT;
    default:             return d->secret ? (d->digits_only ? EL_UI_ED_PIN : EL_UI_ED_SECRET) : EL_UI_ED_TEXT;
    }
}

size_t el_ui_set_group_count(el_ui_set_group_t g)
{
    size_t n = 0;
    for (int i = 0; i < EL_SET_COUNT; i++) if (el_ui_set_group((el_set_id_t)i) == g) n++;
    return n;
}

el_set_id_t el_ui_set_group_item(el_ui_set_group_t g, size_t n)
{
    for (int i = 0; i < EL_SET_COUNT; i++) {
        if (el_ui_set_group((el_set_id_t)i) != g) continue;
        if (n-- == 0) return (el_set_id_t)i;
    }
    return EL_SET_COUNT;
}

const char *el_ui_set_enum_text(el_set_id_t id, uint8_t idx)
{
    const el_set_def_t *d = el_settings_def(id);
    if (!d || d->type != EL_SET_T_ENUM || !d->enum_names || idx >= d->enum_count) return NULL;
    const char *name = d->enum_names[idx];
    for (size_t i = 0; i < UI_ENUM_N; i++)
        if (!strcmp(UI_ENUM[i].key, d->key) && !strcmp(UI_ENUM[i].name, name)) return UI_ENUM[i].text;
    return name;
}

static el_err_t out_str(char *buf, size_t cap, const char *s)
{
    return el_strlcpy(buf, s, cap) >= cap ? EL_ERR_SIZE : EL_OK;
}

el_err_t el_ui_set_value_text(const el_settings_t *s, el_set_id_t id, char *buf, size_t cap)
{
    const el_set_def_t *d = el_settings_def(id);
    if (!s || !d || !buf || !cap) return EL_ERR_ARG;
    char raw[48];
    if (el_settings_get_str(s, id, raw, sizeof raw) == EL_ERR_ARG) return EL_ERR_ARG;
    switch (d->type) {
    case EL_SET_T_BOOL:
        return out_str(buf, cap, raw[0] == '1' ? "On" : "Off");
    case EL_SET_T_ENUM: {
        for (uint8_t i = 0; i < d->enum_count; i++)
            if (!strcmp(d->enum_names[i], raw)) return out_str(buf, cap, el_ui_set_enum_text(id, i));
        return out_str(buf, cap, raw);
    }
    case EL_SET_T_STRING:
        if (d->secret) return out_str(buf, cap, raw[0] ? "Set" : "Not set");
        return out_str(buf, cap, raw[0] ? raw : "(none)");
    default:
        break;
    }
    /* Numbers: "<value> = off" / "<value> = never" from the schema help text. */
    if (d->help) {
        char pat[64];
        snprintf(pat, sizeof pat, "%s = off", raw);
        if (strstr(d->help, pat)) return out_str(buf, cap, "Off");
        snprintf(pat, sizeof pat, "%s = never", raw);
        if (strstr(d->help, pat)) return out_str(buf, cap, "Never");
    }
    if (!strcmp(d->key, "log_interval_s")) {         /* "5 min", "1 h 30 min" */
        el_err_t e = el_ui_fmt_secs((uint32_t)strtoul(raw, NULL, 10), buf, cap);
        return e;
    }
    if (!strcmp(d->key, "utc_offset_min")) {
        int m = atoi(raw);
        int a = m < 0 ? -m : m;
        int n = snprintf(buf, cap, "UTC%c%02d:%02d", m < 0 ? '-' : '+', a / 60, a % 60);
        return (n < 0 || (size_t)n >= cap) ? EL_ERR_SIZE : EL_OK;
    }
    bool unit = d->unit && d->unit[0];
    /* The schema keeps plain ASCII units for the serial port; the screen has a degree sign. */
    const char *u = !unit ? "" : !strcmp(d->unit, "C") ? "\xC2\xB0" "C" : d->unit;
    int n = snprintf(buf, cap, "%s%s%s", raw, unit ? " " : "", u);
    return (n < 0 || (size_t)n >= cap) ? EL_ERR_SIZE : EL_OK;
}

bool el_ui_set_writable(el_set_id_t id, el_level_t level)
{
    const el_set_def_t *d = el_settings_def(id);
    return d && level >= d->write_level;
}
