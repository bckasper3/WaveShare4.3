#include <stddef.h>
#include <string.h>
#include "el_access.h"

/* The permission table. Changing a level here changes it on every interface. */
static const uint8_t MIN_LEVEL[EL_ACT_COUNT] = {
    [EL_ACT_MEASURE]          = EL_LEVEL_OPERATOR,
    [EL_ACT_VIEW_HISTORY]     = EL_LEVEL_OPERATOR,
    [EL_ACT_EXPORT]           = EL_LEVEL_OPERATOR,
    [EL_ACT_VIEW_DIAGNOSTICS] = EL_LEVEL_OPERATOR,
    [EL_ACT_EDIT_SETTINGS]    = EL_LEVEL_ADMIN,
    [EL_ACT_EDIT_LIMITS]      = EL_LEVEL_ADMIN,
    [EL_ACT_SET_TIME]         = EL_LEVEL_ADMIN,
    [EL_ACT_CALIBRATE]        = EL_LEVEL_ADMIN,
    [EL_ACT_DELETE_LOG]       = EL_LEVEL_ADMIN,
    [EL_ACT_FW_UPDATE]        = EL_LEVEL_ADMIN,
    [EL_ACT_CHANGE_ADMIN_PIN] = EL_LEVEL_ADMIN,
    [EL_ACT_RAW_IO]           = EL_LEVEL_ENGINEER,
    [EL_ACT_RAW_I2C]          = EL_LEVEL_ENGINEER,
    [EL_ACT_SENSOR_RAW]       = EL_LEVEL_ENGINEER,
    [EL_ACT_PROVISION]        = EL_LEVEL_ENGINEER,
    [EL_ACT_SIMULATION]       = EL_LEVEL_ENGINEER,
    [EL_ACT_FACTORY_RESET]    = EL_LEVEL_ENGINEER,
    [EL_ACT_DIAG_BOOT]        = EL_LEVEL_ENGINEER,
};

const char *el_level_name(el_level_t level)
{
    static const char *const N[EL_LEVEL_COUNT] = { "OPERATOR", "ADMIN", "ENGINEER" };
    return (unsigned)level < EL_LEVEL_COUNT ? N[level] : "UNKNOWN";
}

el_level_t el_action_min_level(el_action_t a)
{
    return (unsigned)a < EL_ACT_COUNT ? (el_level_t)MIN_LEVEL[a] : EL_LEVEL_ENGINEER;
}

bool el_access_allows(el_level_t current, el_action_t a)
{
    return (unsigned)current < EL_LEVEL_COUNT && current >= el_action_min_level(a);
}

/* Elapsed-time comparison that survives the 49-day wrap of a 32-bit ms counter. */
static bool time_before(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) < 0; }

/* Compares every byte of both (bounded) strings so timing does not reveal the matching prefix. */
static bool code_equal(const char *a, const char *b)
{
    size_t la = strnlen(a, EL_CODE_MAX_LEN + 1), lb = strnlen(b, EL_CODE_MAX_LEN + 1);
    unsigned diff = (unsigned)(la != lb) | (unsigned)(la > EL_CODE_MAX_LEN);
    for (size_t i = 0; i < EL_CODE_MAX_LEN; i++) {
        unsigned ca = i < la ? (unsigned char)a[i] : 0u;
        unsigned cb = i < lb ? (unsigned char)b[i] : 0u;
        diff |= ca ^ cb;
    }
    return diff == 0;
}

void el_session_init(el_session_t *s, uint32_t idle_timeout_ms, uint32_t now_ms)
{
    memset(s, 0, sizeof *s);
    s->level = EL_LEVEL_OPERATOR;
    s->idle_timeout_ms = idle_timeout_ms;
    s->last_activity_ms = now_ms;
}

static uint32_t lock_duration(uint8_t failures)
{
    uint32_t ms = EL_LOGIN_LOCK_MS;
    for (unsigned i = EL_LOGIN_FREE_TRIES; i < failures && ms < EL_LOGIN_LOCK_MAX_MS; i++) ms *= 2;
    return ms > EL_LOGIN_LOCK_MAX_MS ? EL_LOGIN_LOCK_MAX_MS : ms;
}

el_login_t el_session_login(el_session_t *s, el_level_t want, const char *entered,
                            const char *expected, uint32_t now_ms, uint32_t *retry_in_ms)
{
    if (want != EL_LEVEL_ADMIN && want != EL_LEVEL_ENGINEER) return EL_LOGIN_BAD_LEVEL;
    if (s->locked) {
        if (time_before(now_ms, s->locked_until_ms)) {
            if (retry_in_ms) *retry_in_ms = s->locked_until_ms - now_ms;
            return EL_LOGIN_LOCKED;
        }
        s->locked = false;
    }
    bool no_code = !expected || !expected[0];
    if (no_code && want == EL_LEVEL_ENGINEER) return EL_LOGIN_DISABLED;

    if (no_code || code_equal(entered ? entered : "", expected)) {
        s->level = want;
        s->failures = 0;
        s->last_activity_ms = now_ms;
        return EL_LOGIN_OK;
    }
    if (s->failures < UINT8_MAX) s->failures++;
    if (s->failures >= EL_LOGIN_FREE_TRIES) {
        uint32_t d = lock_duration(s->failures);
        s->locked = true;
        s->locked_until_ms = now_ms + d;
        if (retry_in_ms) *retry_in_ms = d;
    }
    return EL_LOGIN_WRONG;
}

void el_session_logout(el_session_t *s) { s->level = EL_LEVEL_OPERATOR; }

void el_session_touch(el_session_t *s, uint32_t now_ms) { s->last_activity_ms = now_ms; }

el_level_t el_session_level(el_session_t *s, uint32_t now_ms)
{
    if (s->level != EL_LEVEL_OPERATOR && s->idle_timeout_ms &&
        !time_before(now_ms, s->last_activity_ms + s->idle_timeout_ms)) {
        s->level = EL_LEVEL_OPERATOR;
    }
    return s->level;
}
