#pragma once
/* All user/device settings: ONE schema table (el_settings_def) drives the touchscreen Settings
 * screens, the serial SET?/SET commands, Modbus holding registers and persistence. Nothing about
 * a setting may be hard-coded elsewhere: look it up here.
 *
 * Not thread-safe; the caller owns the el_settings_t and serialises access.
 *
 * Error mapping for el_settings_set_str():
 *   EL_OK                 applied
 *   EL_SET_ERR_DENIED     'who' may not write this setting (serial: ERR 4). Checked FIRST, so a
 *                         denied caller learns nothing about the value's validity.
 *   EL_ERR_ARG            NULL/unknown id, or text that does not parse (not a number, unknown
 *                         enum/bool word, non-printable or non-digit characters)
 *   EL_ERR_RANGE          parsed, but out of the setting's range / string length, or the whole
 *                         struct would break a cross-field rule (see el_settings_validate)
 * On every error the struct is left unchanged. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_access.h"
#include "el_err.h"
#include "el_result.h"
#include "el_uplink.h"

/* Kept as an alias so existing callers read naturally; same value as EL_ERR_DENIED. */
#define EL_SET_ERR_DENIED   EL_ERR_DENIED

/* Order = display order. The numeric ID is NOT persisted (the TAG is); reorder freely. */
typedef enum {
    /* identity (ENGINEER) */
    EL_SET_DEVICE_SERIAL = 0,
    /* measurement and logging (ADMIN) */
    EL_SET_REPEATS,
    EL_SET_DECIMALS,
    EL_SET_LOG_INTERVAL,
    EL_SET_LOCATION,
    EL_SET_T_LOW,
    EL_SET_T_HIGH,
    EL_SET_RH_LOW,
    EL_SET_RH_HIGH,
    EL_SET_T_OFFSET,
    EL_SET_RH_OFFSET,
    EL_SET_SENSOR_TYPE,
    EL_SET_SENSOR_ADDR,
    /* display (ADMIN) */
    EL_SET_BRIGHTNESS,
    EL_SET_IDLE_DIM,
    EL_SET_IDLE_OFF,
    EL_SET_DATE_FORMAT,
    EL_SET_UTC_OFFSET,
    /* comms (ADMIN) */
    EL_SET_MB_ADDR,
    EL_SET_MB_BAUD,
    EL_SET_MB_PARITY,
    EL_SET_SERIAL_STREAM,
    EL_SET_DO1_MODE,
    EL_SET_DI0_TRIGGER,
    /* security */
    EL_SET_ADMIN_PIN,
    EL_SET_LOGIN_IDLE,
    EL_SET_ENGINEER_CODE,
    /* operator-level */
    EL_SET_OPERATOR_ID,
    EL_SET_THEME,
    EL_SET_HOME_RETURN,
    /* Wi-Fi, send-only result upload (docs/WIFI.md). All ADMIN, off by default. */
    EL_SET_WIFI_ENABLED,
    EL_SET_WIFI_SSID,
    EL_SET_WIFI_PASSWORD,
    EL_SET_UPLOAD_URL,
    EL_SET_UPLOAD_KEY,
    EL_SET_WIFI_TIME_SYNC,
    EL_SET_COUNT
} el_set_id_t;

/* Enum values (stored as the index into the setting's name list). */
typedef enum { EL_DATE_ISO = 0, EL_DATE_US, EL_DATE_EU } el_date_fmt_t;
typedef enum { EL_BAUD_9600 = 0, EL_BAUD_19200, EL_BAUD_38400, EL_BAUD_57600, EL_BAUD_115200 } el_baud_t;
typedef enum { EL_PARITY_NONE = 0, EL_PARITY_EVEN, EL_PARITY_ODD } el_parity_t;
typedef enum { EL_THEME_LIGHT = 0, EL_THEME_DARK } el_theme_t;
typedef enum { EL_DO1_OFF = 0, EL_DO1_ON_ALARM, EL_DO1_BUSY } el_do1_mode_t;
typedef enum { EL_SENSOR_SHT4X = 0, EL_SENSOR_SHT3X } el_sensor_type_t;
typedef enum { EL_SENSOR_ADDR_44 = 0, EL_SENSOR_ADDR_45 } el_sensor_addr_t;

#define EL_SET_SERIAL_MAX     15
#define EL_SET_PIN_MAX        8
#define EL_SET_LOCATION_MAX   19    /* EL_TAG_LEN - 1: must fit the log record */
#define EL_SET_OPERATOR_MAX   7     /* EL_OPERATOR_LEN - 1: must fit the log record */
/* Factory default for both the Admin PIN and the engineer code. Change both after setup. */
#define EL_DEFAULT_PIN        "1234"

typedef struct {
    char     device_serial[EL_SET_SERIAL_MAX + 1];
    uint8_t  repeats;
    uint8_t  decimals;
    uint32_t log_interval_s;      /* 0 = automatic logging off */
    char     location[EL_SET_LOCATION_MAX + 1];
    float    temp_low_c, temp_high_c;
    float    rh_low_pct, rh_high_pct;
    float    temp_offset_c;       /* calibration offsets */
    float    rh_offset_pct;
    uint8_t  sensor_type;         /* el_sensor_type_t */
    uint8_t  sensor_addr;         /* el_sensor_addr_t */
    uint8_t  brightness_pct;
    uint16_t idle_dim_s;          /* 0 = never */
    uint16_t idle_off_s;
    uint8_t  date_format;         /* el_date_fmt_t */
    int16_t  utc_offset_min;
    uint8_t  mb_addr;
    uint8_t  mb_baud;             /* el_baud_t; use el_settings_mb_baud_bps() for the number */
    uint8_t  mb_parity;           /* el_parity_t */
    bool     serial_stream;
    uint8_t  do1_mode;            /* el_do1_mode_t */
    bool     di0_trigger;
    char     admin_pin[EL_SET_PIN_MAX + 1];       /* SECRET; "" = no PIN */
    uint16_t login_idle_s;
    char     engineer_code[EL_SET_PIN_MAX + 1];   /* SECRET; 4..8 digits */
    char     operator_id[EL_SET_OPERATOR_MAX + 1];
    uint8_t  theme;               /* el_theme_t */
    uint16_t home_return_s;       /* back to Home this long after a result; 0 = stay */
    bool     wifi_enabled;                      /* radio off unless an Admin turns it on */
    char     wifi_ssid[EL_UPLINK_SSID_MAX + 1];
    char     wifi_password[EL_UPLINK_PASS_MAX + 1];   /* SECRET; "" = open network */
    char     upload_url[EL_UPLINK_URL_MAX + 1];       /* "" = connect only, send nothing */
    char     upload_key[EL_UPLINK_TOKEN_MAX + 1];     /* SECRET; "" = no Authorization header */
    bool     wifi_time_sync;                    /* set the clock from internet time (SNTP) */
} el_settings_t;

typedef enum { EL_SET_T_INT, EL_SET_T_FLOAT, EL_SET_T_ENUM, EL_SET_T_BOOL, EL_SET_T_STRING } el_set_type_t;
typedef enum { EL_SET_S_U8, EL_SET_S_U16, EL_SET_S_I16, EL_SET_S_U32, EL_SET_S_F32, EL_SET_S_CHARS } el_set_store_t;

typedef struct {
    const char          *key;          /* lower_snake_case, unique, case-insensitive on lookup */
    uint16_t             tag;          /* stable persistence ID: never reused or renumbered */
    el_set_type_t        type;
    el_set_store_t       store;        /* in-struct representation (for the generic accessors) */
    uint16_t             offset;       /* offsetof(el_settings_t, field) */
    el_level_t           write_level;  /* minimum level that may write; everyone may read */
    float                min, max;     /* numeric range; strings: min/max length; enum: 0..n-1 */
    float                def_num;      /* default for numeric/enum/bool (enum: index) */
    const char          *def_str;      /* default for strings */
    const char *const   *enum_names;   /* enums only, upper case */
    uint8_t              enum_count;
    bool                 digits_only;  /* strings: only '0'..'9' allowed */
    const char          *unit;         /* "" if none */
    const char          *help;
    bool                 secret;       /* never printed; get_str gives "****" / "" */
} el_set_def_t;

/* Schema lookup. def() returns NULL for id >= EL_SET_COUNT. find() is case-insensitive and
 * returns EL_SET_COUNT for NULL/unknown. */
const el_set_def_t *el_settings_def(el_set_id_t id);
el_set_id_t         el_settings_find(const char *key);

void el_settings_defaults(el_settings_t *s);

/* Checks every field against the schema, then the cross-field rules:
 *   temp_low_c < temp_high_c and rh_low_pct < rh_high_pct
 *   log_interval_s is 0 or 10..86400
 *   idle_dim_s and idle_off_s both > 0                     =>  idle_off_s > idle_dim_s
 * Returns EL_OK, EL_ERR_ARG (NULL) or EL_ERR_RANGE. */
el_err_t el_settings_validate(const el_settings_t *s);

/* Parse + permission-check + range-check + cross-field-check, then apply atomically.
 * Text accepted: integers (optional sign, surrounding blanks ignored), decimals ("0.88"),
 * enum names case-insensitively, bool as 0/1/ON/OFF/TRUE/FALSE/YES/NO. Strings are taken
 * verbatim (printable ASCII, no trimming). See the header comment for error codes. */
el_err_t el_settings_set_str(el_settings_t *s, el_set_id_t id, const char *text, el_level_t who);

/* Canonical text: ints decimal, floats shortest "%g" form, enum upper-case name, bool "1"/"0",
 * strings verbatim; SECRET -> "****" when set, "" when empty. Readable at every level.
 * EL_ERR_SIZE if cap is too small (buf is still NUL-terminated and truncated). */
el_err_t el_settings_get_str(const el_settings_t *s, el_set_id_t id, char *buf, size_t cap);

/* One parseable line for SET?, space-separated key=value tokens, value LAST (it may contain
 * spaces):
 *   key=mb_baud type=enum range=9600|19200|38400|57600|115200 level=ADMIN unit=- default=19200 value=19200
 * type = int|float|enum|bool|string; range = min..max (strings: min..max LENGTH); unit "-" if none.
 * describe() has no value= token (schema only); describe_value() appends it (masked if secret). */
el_err_t el_settings_describe(el_set_id_t id, char *buf, size_t cap);
el_err_t el_settings_describe_value(const el_settings_t *s, el_set_id_t id, char *buf, size_t cap);

/* Modbus baud rate in bit/s for s->mb_baud (19200 if the index is invalid). */
uint32_t el_settings_mb_baud_bps(const el_settings_t *s);

/* ---- Persistence ----
 * Layout (little-endian):  magic u16 | version u8 | reserved u8 | payload_len u16 |
 *   payload = repeated { tag u16 | len u8 | value[len] } | crc32 u32 over all preceding bytes.
 * Values: INT = i32, FLOAT = f32, ENUM/BOOL = u8, STRING = raw chars (no NUL).
 * Adding a setting = adding a table row with a new tag; no migration. */
#define EL_SETTINGS_MAGIC          0x5E77u
#define EL_SETTINGS_VERSION        1
#define EL_SETTINGS_ENCODED_MAX    768    /* worst case with every string at maximum length (test_settings checks) */

typedef struct {
    uint8_t unknown;      /* tags present in the data but not in this firmware (newer firmware) */
    uint8_t missing;      /* settings with no entry: kept at their default */
    uint8_t defaulted;    /* stored values rejected (bad range/length/type) and reset to default */
} el_settings_report_t;

/* EL_ERR_ARG (NULL), EL_ERR_SIZE (cap too small). *len = bytes written. */
el_err_t el_settings_encode(const el_settings_t *s, uint8_t *buf, size_t cap, size_t *len);

/* On ANY error *s is filled with defaults. EL_ERR_FORMAT (short, bad magic, malformed TLV),
 * EL_ERR_VERSION (newer layout), EL_ERR_CRC. Trailing bytes after the CRC are ignored.
 * Stored values that fail range/cross-field checks fall back to defaults and are counted in
 * the report (report may be NULL); the call still returns EL_OK. */
el_err_t el_settings_decode(const uint8_t *buf, size_t len, el_settings_t *s);
el_err_t el_settings_decode_ex(const uint8_t *buf, size_t len, el_settings_t *s,
                               el_settings_report_t *report);

/* The alarm limits held in the settings. */
el_limits_t el_settings_limits(const el_settings_t *s);
