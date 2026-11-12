#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "el_settings.h"
#include "el_crc.h"
#include "el_util.h"
#include "el_autolog.h"
#include "el_meas.h"
#include "el_stats.h"

/* ---- The schema: the ONLY place a setting's name, range, default or permission lives. ----
 * TAGs are persisted: never renumber or reuse one, even after removing a setting. */
static const char *const NAMES_DATE[]   = { "ISO", "US", "EU" };
static const char *const NAMES_BAUD[]   = { "9600", "19200", "38400", "57600", "115200" };
static const char *const NAMES_PARITY[] = { "NONE", "EVEN", "ODD" };
static const char *const NAMES_THEME[]  = { "LIGHT", "DARK" };
static const char *const NAMES_DO1[]    = { "OFF", "ON_ALARM", "BUSY" };
static const char *const NAMES_SENSOR[] = { "SHT4X", "SHT3X" };
static const char *const NAMES_ADDR[]   = { "0X44", "0X45" };
static const uint32_t BAUD_BPS[] = { 9600, 19200, 38400, 57600, 115200 };
_Static_assert(sizeof BAUD_BPS / sizeof BAUD_BPS[0] == 5, "baud names and rates must match");

#define OFS(f)  ((uint16_t)offsetof(el_settings_t, f))
#define ARRLEN(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))
#define L_OP  EL_LEVEL_OPERATOR
#define L_AD  EL_LEVEL_ADMIN
#define L_EN  EL_LEVEL_ENGINEER

#define INT_(id, k, tag, st, f, lv, mn, mx, df, un, hlp) \
    [id] = { k, tag, EL_SET_T_INT, st, OFS(f), lv, (float)(mn), (float)(mx), (float)(df), NULL, NULL, 0, false, un, hlp, false }
#define FLT_(id, k, tag, f, lv, mn, mx, df, un, hlp) \
    [id] = { k, tag, EL_SET_T_FLOAT, EL_SET_S_F32, OFS(f), lv, (float)(mn), (float)(mx), (float)(df), NULL, NULL, 0, false, un, hlp, false }
#define ENM_(id, k, tag, f, lv, names, df, hlp) \
    [id] = { k, tag, EL_SET_T_ENUM, EL_SET_S_U8, OFS(f), lv, 0.0f, (float)(ARRLEN(names) - 1), (float)(df), NULL, names, ARRLEN(names), false, "", hlp, false }
#define BOOL_(id, k, tag, f, lv, df, hlp) \
    [id] = { k, tag, EL_SET_T_BOOL, EL_SET_S_U8, OFS(f), lv, 0.0f, 1.0f, (float)(df), NULL, NULL, 0, false, "", hlp, false }
#define STR_(id, k, tag, f, lv, mnl, mxl, df, dig, sec, hlp) \
    [id] = { k, tag, EL_SET_T_STRING, EL_SET_S_CHARS, OFS(f), lv, (float)(mnl), (float)(mxl), 0.0f, df, NULL, 0, dig, "", hlp, sec }

static const el_set_def_t SCHEMA[EL_SET_COUNT] = {
    STR_(EL_SET_DEVICE_SERIAL, "device_serial", 1, device_serial, L_EN, 1, EL_SET_SERIAL_MAX, "EL-0001", false, false,
         "Unit serial number, printed in every result"),

    INT_(EL_SET_REPEATS, "repeats", 10, EL_SET_S_U8, repeats, L_AD, 1, 10, 3, "",
         "Sensor readings averaged per result"),
    INT_(EL_SET_DECIMALS, "decimals", 11, EL_SET_S_U8, decimals, L_AD, 0, 2, 1, "",
         "Decimal places shown for temperature and humidity"),
    INT_(EL_SET_LOG_INTERVAL, "log_interval_s", 12, EL_SET_S_U32, log_interval_s, L_AD, 0, EL_AUTOLOG_MAX_S, 300, "s",
         "Automatic logging interval, 10 s to 24 h; 0 = off (manual readings only)"),
    STR_(EL_SET_LOCATION, "location", 13, location, L_AD, 0, EL_SET_LOCATION_MAX, "", false, false,
         "Location name stored with every result"),
    FLT_(EL_SET_T_LOW, "temp_low_c", 14, temp_low_c, L_AD, -40, 125, 5, "C",
         "Low temperature alarm; -40 = off; must be below temp_high_c"),
    FLT_(EL_SET_T_HIGH, "temp_high_c", 15, temp_high_c, L_AD, -40, 125, 35, "C",
         "High temperature alarm; 125 = off"),
    FLT_(EL_SET_RH_LOW, "rh_low_pct", 16, rh_low_pct, L_AD, 0, 100, 20, "%RH",
         "Low humidity alarm; 0 = off; must be below rh_high_pct"),
    FLT_(EL_SET_RH_HIGH, "rh_high_pct", 17, rh_high_pct, L_AD, 0, 100, 80, "%RH",
         "High humidity alarm; 100 = off"),
    FLT_(EL_SET_T_OFFSET, "temp_offset_c", 18, temp_offset_c, L_AD, -EL_MEAS_T_OFFSET_MAX_C, EL_MEAS_T_OFFSET_MAX_C, 0, "C",
         "Calibration: added to every temperature reading"),
    FLT_(EL_SET_RH_OFFSET, "rh_offset_pct", 19, rh_offset_pct, L_AD, -EL_MEAS_RH_OFFSET_MAX_PCT, EL_MEAS_RH_OFFSET_MAX_PCT, 0, "%RH",
         "Calibration: added to every humidity reading"),
    ENM_(EL_SET_SENSOR_TYPE, "sensor_type", 5, sensor_type, L_AD, NAMES_SENSOR, EL_SENSOR_SHT4X,
         "Sensor family on the I2C terminal; applies after a restart"),
    ENM_(EL_SET_SENSOR_ADDR, "sensor_addr", 6, sensor_addr, L_AD, NAMES_ADDR, EL_SENSOR_ADDR_44,
         "Sensor I2C address; applies after a restart"),

    INT_(EL_SET_BRIGHTNESS, "brightness_pct", 20, EL_SET_S_U8, brightness_pct, L_AD, 0, 100, 100, "%",
         "Backlight brightness"),
    INT_(EL_SET_IDLE_DIM, "idle_dim_s", 21, EL_SET_S_U16, idle_dim_s, L_AD, 0, 3600, 120, "s",
         "Dim the screen after this idle time; 0 = never"),
    INT_(EL_SET_IDLE_OFF, "idle_off_s", 22, EL_SET_S_U16, idle_off_s, L_AD, 0, 7200, 600, "s",
         "Switch the screen off after this idle time; 0 = never; must exceed idle_dim_s"),
    ENM_(EL_SET_DATE_FORMAT, "date_format", 23, date_format, L_AD, NAMES_DATE, EL_DATE_ISO,
         "Date display format"),
    INT_(EL_SET_UTC_OFFSET, "utc_offset_min", 24, EL_SET_S_I16, utc_offset_min, L_AD, -720, 840, 0, "min",
         "Local time offset from UTC"),

    INT_(EL_SET_MB_ADDR, "mb_addr", 30, EL_SET_S_U8, mb_addr, L_AD, 1, 247, 1, "",
         "Modbus RTU slave address"),
    ENM_(EL_SET_MB_BAUD, "mb_baud", 31, mb_baud, L_AD, NAMES_BAUD, EL_BAUD_19200,
         "Modbus baud rate (bit/s)"),
    ENM_(EL_SET_MB_PARITY, "mb_parity", 32, mb_parity, L_AD, NAMES_PARITY, EL_PARITY_EVEN,
         "Modbus parity"),
    BOOL_(EL_SET_SERIAL_STREAM, "serial_stream", 33, serial_stream, L_AD, false,
          "Stream each result on the USB serial port"),
    ENM_(EL_SET_DO1_MODE, "do1_mode", 34, do1_mode, L_AD, NAMES_DO1, EL_DO1_ON_ALARM,
         "When digital output DO1 is active"),
    BOOL_(EL_SET_DI0_TRIGGER, "di0_trigger", 35, di0_trigger, L_AD, false,
          "A pulse on digital input DI0 starts a measurement"),

    STR_(EL_SET_ADMIN_PIN, "admin_pin", 40, admin_pin, L_AD, 0, EL_SET_PIN_MAX, EL_DEFAULT_PIN, true, true,
         "Admin PIN, digits only; empty = no PIN. Change the default"),
    INT_(EL_SET_LOGIN_IDLE, "login_idle_s", 41, EL_SET_S_U16, login_idle_s, L_AD, 60, 3600, 300, "s",
         "Automatic logout after this idle time"),
    STR_(EL_SET_ENGINEER_CODE, "engineer_code", 42, engineer_code, L_EN, 4, EL_SET_PIN_MAX, EL_DEFAULT_PIN, true, true,
         "Engineer (service) login code, 4-8 digits. Change the default"),

    STR_(EL_SET_OPERATOR_ID, "operator_id", 50, operator_id, L_OP, 0, EL_SET_OPERATOR_MAX, "", false, false,
         "Operator initials/ID stored with each result"),

    ENM_(EL_SET_THEME, "theme", 25, theme, L_OP, NAMES_THEME, EL_THEME_LIGHT,
         "Screen colours: LIGHT for bright rooms, DARK for dim ones"),
    INT_(EL_SET_HOME_RETURN, "home_return_s", 26, EL_SET_S_U16, home_return_s, L_OP, 0, 600, 0, "s",
         "Go back to the Home screen this long after a result; 0 = never"),

    /* Wi-Fi: send-only upload of results (docs/WIFI.md). Off by default; Admin only. */
    BOOL_(EL_SET_WIFI_ENABLED, "wifi_enabled", 60, wifi_enabled, L_AD, false,
          "Wi-Fi on: send each result to the upload server (send-only)"),
    STR_(EL_SET_WIFI_SSID, "wifi_ssid", 61, wifi_ssid, L_AD, 0, EL_UPLINK_SSID_MAX, "", false, false,
         "Wi-Fi network name"),
    STR_(EL_SET_WIFI_PASSWORD, "wifi_password", 62, wifi_password, L_AD, 0, EL_UPLINK_PASS_MAX, "", false, true,
         "Wi-Fi password, 8-63 characters; empty = open network"),
    STR_(EL_SET_UPLOAD_URL, "upload_url", 63, upload_url, L_AD, 0, EL_UPLINK_URL_MAX, "", false, false,
         "Server address results are sent to (https://...); empty = send nothing"),
    STR_(EL_SET_UPLOAD_KEY, "upload_key", 64, upload_key, L_AD, 0, EL_UPLINK_TOKEN_MAX, "", false, true,
         "Access key for the server (sent as a Bearer token); empty = none"),
    BOOL_(EL_SET_WIFI_TIME_SYNC, "wifi_time_sync", 65, wifi_time_sync, L_AD, false,
          "Set the clock from internet time while Wi-Fi is connected"),
};

const el_set_def_t *el_settings_def(el_set_id_t id)
{
    return ((unsigned)id < EL_SET_COUNT) ? &SCHEMA[id] : NULL;
}

static bool ci_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    return *a == *b;
}

el_set_id_t el_settings_find(const char *key)
{
    if (!key) return EL_SET_COUNT;
    for (int i = 0; i < EL_SET_COUNT; i++)
        if (ci_eq(key, SCHEMA[i].key)) return (el_set_id_t)i;
    return EL_SET_COUNT;
}

/* ---- Generic field access through the schema ---- */
static char *chars_of(el_settings_t *s, const el_set_def_t *d) { return (char *)s + d->offset; }
static const char *cchars_of(const el_settings_t *s, const el_set_def_t *d) { return (const char *)s + d->offset; }
static size_t str_max(const el_set_def_t *d) { return (size_t)d->max; }

static double get_num(const el_settings_t *s, const el_set_def_t *d)
{
    const uint8_t *p = (const uint8_t *)s + d->offset;
    switch (d->store) {
    case EL_SET_S_U8:  return (double)*p;
    case EL_SET_S_U16: { uint16_t v; memcpy(&v, p, sizeof v); return (double)v; }
    case EL_SET_S_I16: { int16_t v;  memcpy(&v, p, sizeof v); return (double)v; }
    case EL_SET_S_U32: { uint32_t v; memcpy(&v, p, sizeof v); return (double)v; }
    case EL_SET_S_F32: { float v;    memcpy(&v, p, sizeof v); return (double)v; }
    default:           return 0.0;
    }
}

/* v must already satisfy num_ok(). */
static void put_num(el_settings_t *s, const el_set_def_t *d, double v)
{
    uint8_t *p = (uint8_t *)s + d->offset;
    switch (d->store) {
    case EL_SET_S_U8:  *p = (uint8_t)v; break;
    case EL_SET_S_U16: { uint16_t x = (uint16_t)v; memcpy(p, &x, sizeof x); break; }
    case EL_SET_S_I16: { int16_t x = (int16_t)v;   memcpy(p, &x, sizeof x); break; }
    case EL_SET_S_U32: { uint32_t x = (uint32_t)v; memcpy(p, &x, sizeof x); break; }
    case EL_SET_S_F32: { float x = (float)v;       memcpy(p, &x, sizeof x); break; }
    default: break;
    }
}

static bool num_ok(const el_set_def_t *d, double v)
{
    if (!isfinite(v) || v < (double)d->min || v > (double)d->max) return false;
    return d->type == EL_SET_T_FLOAT || v == floor(v);
}

/* Printable ASCII only (also keeps CSV/serial output safe); digits only for PINs. */
static bool chars_ok(const el_set_def_t *d, const char *t, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)t[i];
        if (c < 0x20 || c > 0x7E) return false;
        if (d->digits_only && (c < '0' || c > '9')) return false;
    }
    return true;
}

static void set_default(el_settings_t *s, const el_set_def_t *d)
{
    if (d->type == EL_SET_T_STRING) el_strlcpy(chars_of(s, d), d->def_str, str_max(d) + 1);
    else put_num(s, d, (double)d->def_num);
}

void el_settings_defaults(el_settings_t *s)
{
    if (!s) return;
    memset(s, 0, sizeof *s);
    for (int i = 0; i < EL_SET_COUNT; i++) set_default(s, &SCHEMA[i]);
}

uint32_t el_settings_mb_baud_bps(const el_settings_t *s)
{
    return (s && s->mb_baud < ARRLEN(BAUD_BPS)) ? BAUD_BPS[s->mb_baud] : 19200u;
}

el_err_t el_settings_validate(const el_settings_t *s)
{
    if (!s) return EL_ERR_ARG;
    for (int i = 0; i < EL_SET_COUNT; i++) {
        const el_set_def_t *d = &SCHEMA[i];
        if (d->type == EL_SET_T_STRING) {
            const char *t = cchars_of(s, d);
            size_t n = strnlen(t, str_max(d) + 1);
            if (n > str_max(d) || (float)n < d->min || !chars_ok(d, t, n)) return EL_ERR_RANGE;
        } else if (!num_ok(d, get_num(s, d))) {
            return EL_ERR_RANGE;
        }
    }
    if (!el_uplink_ssid_ok(s->wifi_ssid) || !el_uplink_password_ok(s->wifi_password)) return EL_ERR_RANGE;
    if (!el_uplink_url_ok(s->upload_url)) return EL_ERR_RANGE;
    if (s->temp_low_c >= s->temp_high_c || s->rh_low_pct >= s->rh_high_pct) return EL_ERR_RANGE;
    if (!el_autolog_interval_ok(s->log_interval_s)) return EL_ERR_RANGE;
    if (s->idle_dim_s > 0 && s->idle_off_s > 0 && s->idle_off_s <= s->idle_dim_s) return EL_ERR_RANGE;
    return EL_OK;
}

/* ---- Text parsing ---- */
typedef enum { P_OK, P_SYNTAX, P_RANGE } parse_t;

static void trim(const char *t, const char **b, size_t *n)
{
    while (*t == ' ' || *t == '\t') t++;
    size_t len = strlen(t);
    while (len > 0 && (t[len - 1] == ' ' || t[len - 1] == '\t' || t[len - 1] == '\r' || t[len - 1] == '\n')) len--;
    *b = t;
    *n = len;
}

/* Optional sign + digits only. Values beyond 9 digits are "range", not "syntax". */
static parse_t parse_int(const char *b, size_t n, double *out)
{
    size_t i = 0;
    bool neg = false;
    if (n > 0 && (b[0] == '+' || b[0] == '-')) { neg = (b[0] == '-'); i++; }
    if (i >= n) return P_SYNTAX;
    long v = 0;
    bool big = false;
    for (; i < n; i++) {
        if (b[i] < '0' || b[i] > '9') return P_SYNTAX;
        if (v < 100000000L) v = v * 10 + (b[i] - '0'); else big = true;
    }
    if (big) return P_RANGE;
    *out = (double)(neg ? -v : v);
    return P_OK;
}

/* Decimal numbers only: rejects "nan", "inf", hex and trailing junk. */
static parse_t parse_float(const char *b, size_t n, double *out)
{
    char tmp[32];
    if (n == 0) return P_SYNTAX;
    if (n >= sizeof tmp) return P_RANGE;
    bool digit = false;
    for (size_t i = 0; i < n; i++) {
        char c = b[i];
        if (c >= '0' && c <= '9') digit = true;
        else if (c != '+' && c != '-' && c != '.' && c != 'e' && c != 'E') return P_SYNTAX;
    }
    if (!digit) return P_SYNTAX;
    memcpy(tmp, b, n);
    tmp[n] = '\0';
    char *end;
    float f = strtof(tmp, &end);       /* float, so range checks compare like with like */
    if (*end != '\0') return P_SYNTAX;
    if (!isfinite(f)) return P_RANGE;
    *out = (double)f;
    return P_OK;
}

/* Case-insensitive compare of a (not NUL-terminated) token with a word. */
static bool word_eq(const char *b, size_t n, const char *w)
{
    if (strlen(w) != n) return false;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)b[i]) != tolower((unsigned char)w[i])) return false;
    return true;
}

static parse_t parse_enum(const el_set_def_t *d, const char *b, size_t n, double *out)
{
    for (uint8_t i = 0; i < d->enum_count; i++)
        if (word_eq(b, n, d->enum_names[i])) { *out = (double)i; return P_OK; }
    return P_SYNTAX;
}

static parse_t parse_bool(const char *b, size_t n, double *out)
{
    static const char *const T[] = { "1", "on", "true", "yes" };
    static const char *const F[] = { "0", "off", "false", "no" };
    for (int i = 0; i < 4; i++) {
        if (word_eq(b, n, T[i])) { *out = 1.0; return P_OK; }
        if (word_eq(b, n, F[i])) { *out = 0.0; return P_OK; }
    }
    return P_SYNTAX;
}

/* Parse text for one setting and store it in *s (a scratch copy; caller commits). */
static el_err_t apply_text(el_settings_t *s, const el_set_def_t *d, const char *text)
{
    if (d->type == EL_SET_T_STRING) {
        size_t n = strlen(text);
        if (!chars_ok(d, text, n)) return EL_ERR_ARG;
        if (n > str_max(d) || (float)n < d->min) return EL_ERR_RANGE;
        el_strlcpy(chars_of(s, d), text, str_max(d) + 1);
        return EL_OK;
    }
    const char *b;
    size_t n;
    double v = 0.0;
    trim(text, &b, &n);
    parse_t p;
    switch (d->type) {
    case EL_SET_T_INT:   p = parse_int(b, n, &v);   break;
    case EL_SET_T_FLOAT: p = parse_float(b, n, &v); break;
    case EL_SET_T_ENUM:  p = parse_enum(d, b, n, &v); break;
    default:             p = parse_bool(b, n, &v);  break;
    }
    if (p == P_SYNTAX) return EL_ERR_ARG;
    if (p == P_RANGE || !num_ok(d, v)) return EL_ERR_RANGE;
    put_num(s, d, v);
    return EL_OK;
}

el_err_t el_settings_set_str(el_settings_t *s, el_set_id_t id, const char *text, el_level_t who)
{
    const el_set_def_t *d = el_settings_def(id);
    if (!s || !d || !text || (unsigned)who >= EL_LEVEL_COUNT) return EL_ERR_ARG;
    if (who < d->write_level) return EL_SET_ERR_DENIED;
    el_settings_t tmp = *s;                  /* all-or-nothing: commit only if everything holds */
    el_err_t e = apply_text(&tmp, d, text);
    if (e != EL_OK) return e;
    if (el_settings_validate(&tmp) != EL_OK) return EL_ERR_RANGE;
    *s = tmp;
    return EL_OK;
}

/* ---- Text output ---- */
el_err_t el_settings_get_str(const el_settings_t *s, el_set_id_t id, char *buf, size_t cap)
{
    const el_set_def_t *d = el_settings_def(id);
    if (!s || !d || !buf || cap == 0) return EL_ERR_ARG;
    int n;
    switch (d->type) {
    case EL_SET_T_STRING: {
        const char *t = cchars_of(s, d);
        n = snprintf(buf, cap, "%s", d->secret ? (t[0] ? "****" : "") : t);
        break;
    }
    case EL_SET_T_FLOAT: n = snprintf(buf, cap, "%g", get_num(s, d)); break;
    case EL_SET_T_ENUM: {
        double v = get_num(s, d);
        n = snprintf(buf, cap, "%s", (v >= 0 && v < d->enum_count) ? d->enum_names[(int)v] : "?");
        break;
    }
    case EL_SET_T_BOOL: n = snprintf(buf, cap, "%d", get_num(s, d) != 0.0 ? 1 : 0); break;
    default:            n = snprintf(buf, cap, "%ld", (long)get_num(s, d)); break;
    }
    return (n < 0 || (size_t)n >= cap) ? EL_ERR_SIZE : EL_OK;
}

static const char *type_name(el_set_type_t t)
{
    static const char *const N[] = { "int", "float", "enum", "bool", "string" };
    return N[t];
}

/* Appends to buf at *pos; sets *ok=false on overflow. */
static void put(char *buf, size_t cap, size_t *pos, bool *ok, const char *fmt, ...)
{
    if (!*ok) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *pos, cap - *pos, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - *pos) { *ok = false; return; }
    *pos += (size_t)n;
}

static el_err_t describe(const el_settings_t *s, el_set_id_t id, char *buf, size_t cap)
{
    const el_set_def_t *d = el_settings_def(id);
    if (!d || !buf || cap == 0) return EL_ERR_ARG;
    buf[0] = '\0';
    size_t pos = 0;
    bool ok = true;
    put(buf, cap, &pos, &ok, "key=%s type=%s range=", d->key, type_name(d->type));
    if (d->type == EL_SET_T_ENUM) {
        for (uint8_t i = 0; i < d->enum_count; i++)
            put(buf, cap, &pos, &ok, "%s%s", i ? "|" : "", d->enum_names[i]);
    } else if (d->type == EL_SET_T_BOOL) {
        put(buf, cap, &pos, &ok, "0|1");
    } else {
        put(buf, cap, &pos, &ok, "%g..%g", (double)d->min, (double)d->max);
    }
    el_settings_t def;
    char dv[40];
    el_settings_defaults(&def);
    el_settings_get_str(&def, id, dv, sizeof dv);
    put(buf, cap, &pos, &ok, " level=%s unit=%s default=%s", el_level_name(d->write_level),
        d->unit[0] ? d->unit : "-", dv);
    if (s) {
        char v[40];
        el_settings_get_str(s, id, v, sizeof v);      /* masked for secrets; value goes last */
        put(buf, cap, &pos, &ok, " value=%s", v);
    }
    return ok ? EL_OK : EL_ERR_SIZE;
}

el_err_t el_settings_describe(el_set_id_t id, char *buf, size_t cap)
{
    return describe(NULL, id, buf, cap);
}

el_err_t el_settings_describe_value(const el_settings_t *s, el_set_id_t id, char *buf, size_t cap)
{
    return s ? describe(s, id, buf, cap) : EL_ERR_ARG;
}

/* ---- Persistence ---- */
enum { HDR = 6, CRC_LEN = 4 };

static size_t value_len(const el_settings_t *s, const el_set_def_t *d)
{
    switch (d->type) {
    case EL_SET_T_INT: case EL_SET_T_FLOAT: return 4;
    case EL_SET_T_ENUM: case EL_SET_T_BOOL: return 1;
    default: return strnlen(cchars_of(s, d), str_max(d));
    }
}

static void put_value(uint8_t *p, const el_settings_t *s, const el_set_def_t *d, size_t n)
{
    switch (d->type) {
    case EL_SET_T_INT:   el_put_u32le(p, (uint32_t)(int32_t)get_num(s, d)); break;
    case EL_SET_T_FLOAT: el_put_f32le(p, (float)get_num(s, d)); break;
    case EL_SET_T_ENUM: case EL_SET_T_BOOL: p[0] = (uint8_t)get_num(s, d); break;
    default: memcpy(p, cchars_of(s, d), n); break;
    }
}

el_err_t el_settings_encode(const el_settings_t *s, uint8_t *buf, size_t cap, size_t *len)
{
    if (!s || !buf || !len) return EL_ERR_ARG;
    uint8_t *tmp = buf;                       /* written in place; nothing is written past cap */
    size_t p = HDR;
    if (cap < HDR + CRC_LEN) return EL_ERR_SIZE;
    for (int i = 0; i < EL_SET_COUNT; i++) {
        const el_set_def_t *d = &SCHEMA[i];
        size_t n = value_len(s, d);
        if (p + 3 + n + CRC_LEN > cap) return EL_ERR_SIZE;
        el_put_u16le(tmp + p, d->tag);
        tmp[p + 2] = (uint8_t)n;
        put_value(tmp + p + 3, s, d, n);
        p += 3 + n;
    }
    el_put_u16le(tmp, EL_SETTINGS_MAGIC);
    tmp[2] = EL_SETTINGS_VERSION;
    tmp[3] = 0;
    el_put_u16le(tmp + 4, (uint16_t)(p - HDR));
    el_put_u32le(tmp + p, el_crc32(tmp, p));
    p += CRC_LEN;
    *len = p;
    return EL_OK;
}

static const el_set_def_t *find_tag(uint16_t tag, int *idx)
{
    for (int i = 0; i < EL_SET_COUNT; i++)
        if (SCHEMA[i].tag == tag) { *idx = i; return &SCHEMA[i]; }
    return NULL;
}

/* Store one TLV value into *s if it is well-formed and in range. */
static bool take_value(el_settings_t *s, const el_set_def_t *d, const uint8_t *v, size_t n)
{
    switch (d->type) {
    case EL_SET_T_INT: {
        if (n != 4) return false;
        double x = (double)(int32_t)el_get_u32le(v);
        if (!num_ok(d, x)) return false;
        put_num(s, d, x);
        return true;
    }
    case EL_SET_T_FLOAT: {
        if (n != 4) return false;
        double x = (double)el_get_f32le(v);
        if (!num_ok(d, x)) return false;
        put_num(s, d, x);
        return true;
    }
    case EL_SET_T_ENUM: case EL_SET_T_BOOL: {
        if (n != 1 || !num_ok(d, (double)v[0])) return false;
        put_num(s, d, (double)v[0]);
        return true;
    }
    default: {
        if (n > str_max(d) || (float)n < d->min || !chars_ok(d, (const char *)v, n)) return false;
        char *dst = chars_of(s, d);
        memset(dst, 0, str_max(d) + 1);
        memcpy(dst, v, n);
        return true;
    }
    }
}

static void bump(uint8_t *c) { if (*c < 255) (*c)++; }

el_err_t el_settings_decode_ex(const uint8_t *buf, size_t len, el_settings_t *s,
                               el_settings_report_t *report)
{
    el_settings_report_t rep = {0, 0, 0};
    if (report) *report = rep;
    if (!s) return EL_ERR_ARG;
    el_settings_defaults(s);
    if (!buf) return EL_ERR_ARG;
    if (len < HDR + CRC_LEN || el_get_u16le(buf) != EL_SETTINGS_MAGIC || buf[2] == 0) return EL_ERR_FORMAT;
    if (buf[2] > EL_SETTINGS_VERSION) return EL_ERR_VERSION;
    size_t end = HDR + el_get_u16le(buf + 4);
    if (len < end + CRC_LEN) return EL_ERR_FORMAT;       /* truncated */
    if (el_get_u32le(buf + end) != el_crc32(buf, end)) return EL_ERR_CRC;

    el_settings_t t;
    bool present[EL_SET_COUNT] = { false };
    el_settings_defaults(&t);
    for (size_t p = HDR; p < end; ) {
        if (end - p < 3) return EL_ERR_FORMAT;
        uint16_t tag = el_get_u16le(buf + p);
        size_t n = buf[p + 2];
        p += 3;
        if (n > end - p) return EL_ERR_FORMAT;
        int idx;
        const el_set_def_t *d = find_tag(tag, &idx);
        if (!d) bump(&rep.unknown);                      /* from newer firmware: skip */
        else {
            present[idx] = true;
            if (!take_value(&t, d, buf + p, n)) bump(&rep.defaulted);
        }
        p += n;
    }
    for (int i = 0; i < EL_SET_COUNT; i++) if (!present[i]) bump(&rep.missing);
    if (el_settings_validate(&t) != EL_OK) {
        /* Each value was fine alone but they conflict: drop the cross-checked ones. */
        static const el_set_id_t X[] = { EL_SET_T_LOW, EL_SET_T_HIGH, EL_SET_RH_LOW, EL_SET_RH_HIGH,
                                         EL_SET_IDLE_DIM, EL_SET_IDLE_OFF, EL_SET_LOG_INTERVAL };
        for (size_t i = 0; i < sizeof X / sizeof X[0]; i++) set_default(&t, &SCHEMA[X[i]]);
        bump(&rep.defaulted);
        if (el_settings_validate(&t) != EL_OK) el_settings_defaults(&t);
    }
    *s = t;
    if (report) *report = rep;
    return EL_OK;
}

el_err_t el_settings_decode(const uint8_t *buf, size_t len, el_settings_t *s)
{
    return el_settings_decode_ex(buf, len, s, NULL);
}

el_limits_t el_settings_limits(const el_settings_t *s)
{
    el_limits_t l = { EL_TEMP_MIN_C, EL_TEMP_MAX_C, EL_RH_MIN_PCT, EL_RH_MAX_PCT };
    if (s) {
        l.t_low_c = s->temp_low_c;
        l.t_high_c = s->temp_high_c;
        l.rh_low_pct = s->rh_low_pct;
        l.rh_high_pct = s->rh_high_pct;
    }
    return l;
}
