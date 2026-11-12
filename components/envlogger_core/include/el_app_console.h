#pragma once
/* Serial-console helpers (docs/PROTOCOLS.md sections 1 and 1.1, docs/APP.md section 6):
 * argument parsers and the login wrapper used by main/console. Pure C. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_access.h"

#define EL_APP_WATCH_MIN_MS   100u
#define EL_APP_WATCH_MAX_MS   60000u
#define EL_APP_LOG_MAX_LINES  100000u   /* LOG? <n> upper bound */

/* Unsigned integer, decimal or 0x-hex, whole string, within [min, max]. */
bool el_app_parse_u32(const char *s, uint32_t min, uint32_t max, uint32_t *out);
/* One byte in hex, with or without 0x ("40", "0x40", "4"). */
bool el_app_parse_hex_u8(const char *s, uint8_t *out);
/* ON/OFF, 1/0, TRUE/FALSE (case-insensitive). */
bool el_app_parse_onoff(const char *s, bool *out);
/* Finite decimal number, whole string. */
bool el_app_parse_float(const char *s, float *out);
/* Case-insensitive word compare. */
bool el_app_word_eq(const char *a, const char *b);

/* Login with a scheme that only answers "is this code right?" (the engineer code,
 * DIAGNOSTICS.md 2.1) while keeping el_access's lockout and timing rules: the verdict is
 * replayed through el_session_login. enabled = false -> EL_LOGIN_DISABLED (after the lockout
 * check, like el_session_login). */
el_login_t el_app_login_verdict(el_session_t *s, el_level_t want, bool enabled, bool code_ok,
                                uint32_t now_ms, uint32_t *retry_in_ms);
