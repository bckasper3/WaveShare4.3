#include <math.h>
#include <stdlib.h>
#include "el_app_console.h"

static char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }

bool el_app_word_eq(const char *a, const char *b)
{
    if (!a || !b) return false;
    for (; *a && *b; a++, b++) if (up(*a) != up(*b)) return false;
    return *a == *b;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = up(c);
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool el_app_parse_u32(const char *s, uint32_t min, uint32_t max, uint32_t *out)
{
    if (!s || !*s) return false;
    uint64_t v = 0;
    const char *p = s;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        if (!*p) return false;
        for (; *p; p++) {
            int h = hexval(*p);
            if (h < 0) return false;
            v = v * 16u + (uint64_t)h;
            if (v > UINT32_MAX) return false;
        }
    } else {
        for (; *p; p++) {
            if (*p < '0' || *p > '9') return false;
            v = v * 10u + (uint64_t)(*p - '0');
            if (v > UINT32_MAX) return false;
        }
    }
    if (v < min || v > max) return false;
    if (out) *out = (uint32_t)v;
    return true;
}

bool el_app_parse_hex_u8(const char *s, uint8_t *out)
{
    if (!s) return false;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (!*s || !s[0] || (s[1] && s[2])) return false;     /* 1 or 2 digits */
    unsigned v = 0;
    for (; *s; s++) {
        int h = hexval(*s);
        if (h < 0) return false;
        v = v * 16u + (unsigned)h;
    }
    if (out) *out = (uint8_t)v;
    return true;
}

bool el_app_parse_onoff(const char *s, bool *out)
{
    bool v;
    if (el_app_word_eq(s, "ON") || el_app_word_eq(s, "1") || el_app_word_eq(s, "TRUE")) v = true;
    else if (el_app_word_eq(s, "OFF") || el_app_word_eq(s, "0") || el_app_word_eq(s, "FALSE")) v = false;
    else return false;
    if (out) *out = v;
    return true;
}

bool el_app_parse_float(const char *s, float *out)
{
    if (!s || !*s) return false;
    char *end = NULL;
    double d = strtod(s, &end);
    if (!end || *end || !isfinite(d) || d > 3.0e38 || d < -3.0e38) return false;
    if (out) *out = (float)d;
    return true;
}

el_login_t el_app_login_verdict(el_session_t *s, el_level_t want, bool enabled, bool code_ok,
                                uint32_t now_ms, uint32_t *retry_in_ms)
{
    if (!s) return EL_LOGIN_BAD_LEVEL;
    /* Replay the verdict as a fixed comparison so el_session_login applies its own lockout,
     * failure counting and level rules unchanged. */
    const char *expected = enabled ? "1" : NULL;
    const char *entered = code_ok ? "1" : "0";
    return el_session_login(s, want, entered, expected, now_ms, retry_in_ms);
}
