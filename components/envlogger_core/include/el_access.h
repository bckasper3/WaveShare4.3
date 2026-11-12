#pragma once
/* Access levels and the single permission table (docs/DIAGNOSTICS.md section 2).
 * UI and serial console both ask el_access_allows() — never hard-code a level elsewhere.
 * Sessions are per interface (touchscreen, USB serial) and take the time as an argument,
 * so lockout and idle-timeout behaviour is unit-tested without a clock. */
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EL_LEVEL_OPERATOR = 0,
    EL_LEVEL_ADMIN,
    EL_LEVEL_ENGINEER,
    EL_LEVEL_COUNT
} el_level_t;

typedef enum {
    /* operator */
    EL_ACT_MEASURE = 0,
    EL_ACT_VIEW_HISTORY,
    EL_ACT_EXPORT,
    EL_ACT_VIEW_DIAGNOSTICS,     /* read-only: self-test, I/O view */
    /* admin */
    EL_ACT_EDIT_SETTINGS,
    EL_ACT_EDIT_LIMITS,
    EL_ACT_SET_TIME,
    EL_ACT_CALIBRATE,            /* temperature / humidity offsets */
    EL_ACT_DELETE_LOG,
    EL_ACT_FW_UPDATE,
    EL_ACT_CHANGE_ADMIN_PIN,
    /* engineer */
    EL_ACT_RAW_IO,               /* drive DO1 / backlight directly */
    EL_ACT_RAW_I2C,              /* register read/write on any bus address */
    EL_ACT_SENSOR_RAW,
    EL_ACT_PROVISION,            /* serial number */
    EL_ACT_SIMULATION,           /* simulated sensor on/off */
    EL_ACT_FACTORY_RESET,
    EL_ACT_DIAG_BOOT,
    EL_ACT_COUNT
} el_action_t;

const char *el_level_name(el_level_t level);           /* "OPERATOR", "ADMIN", "ENGINEER" */
el_level_t  el_action_min_level(el_action_t action);   /* ENGINEER for unknown actions */
bool        el_access_allows(el_level_t current, el_action_t action);

/* ---- Login sessions ---- */
#define EL_LOGIN_FREE_TRIES     5
#define EL_LOGIN_LOCK_MS        30000u     /* first lockout, doubles each further failure */
#define EL_LOGIN_LOCK_MAX_MS    900000u    /* 15 min */
#define EL_CODE_MAX_LEN         16
/* The Admin PIN and the engineer code are settings (admin_pin, engineer_code), both
 * "1234" from the factory: change them after setup. */

typedef struct {
    el_level_t level;
    uint32_t   idle_timeout_ms;    /* 0 = never time out */
    uint32_t   last_activity_ms;
    uint8_t    failures;
    bool       locked;
    uint32_t   locked_until_ms;
} el_session_t;

typedef enum {
    EL_LOGIN_OK = 0,
    EL_LOGIN_WRONG,        /* wrong code; counts toward lockout */
    EL_LOGIN_LOCKED,       /* too many failures; *retry_in_ms says how long */
    EL_LOGIN_DISABLED,     /* no engineer code configured: engineer login impossible */
    EL_LOGIN_BAD_LEVEL,    /* asked for OPERATOR or an invalid level */
} el_login_t;

void       el_session_init(el_session_t *s, uint32_t idle_timeout_ms, uint32_t now_ms);

/* expected = the configured code for that level. For ADMIN, NULL/"" means no PIN is set and
 * any entry is accepted. For ENGINEER, NULL/"" means disabled. Comparison is constant-time.
 * retry_in_ms (may be NULL) is set when LOCKED or when this failure started a lockout. */
el_login_t el_session_login(el_session_t *s, el_level_t want, const char *entered,
                            const char *expected, uint32_t now_ms, uint32_t *retry_in_ms);
void       el_session_logout(el_session_t *s);
void       el_session_touch(el_session_t *s, uint32_t now_ms);        /* user activity */
el_level_t el_session_level(el_session_t *s, uint32_t now_ms);        /* applies idle timeout */
