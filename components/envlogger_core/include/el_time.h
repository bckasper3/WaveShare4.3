#pragma once
/* UTC calendar conversion without the C library's timezone state (gmtime/mktime depend on TZ).
 * Valid for years 1970..9999. Used for CSV, file names and the serial TIME command. */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "el_err.h"

typedef struct {
    int16_t year;     /* e.g. 2026 */
    uint8_t month;    /* 1..12 */
    uint8_t day;      /* 1..31 */
    uint8_t hour, minute, second;
} el_datetime_t;

void     el_time_to_datetime(int64_t unix_s, el_datetime_t *out);
el_err_t el_datetime_to_time(const el_datetime_t *dt, int64_t *unix_s);   /* validates fields */

/* "2026-10-30T12:00:00Z" (needs cap >= 21). */
el_err_t el_time_format_iso8601(int64_t unix_s, char *buf, size_t cap);
/* "20261030-120000" for file names (needs cap >= 16). */
el_err_t el_time_format_compact(int64_t unix_s, char *buf, size_t cap);
/* Accepts "YYYY-MM-DDTHH:MM:SS" with optional trailing "Z"; a space may replace the 'T'. */
el_err_t el_time_parse_iso8601(const char *s, int64_t *unix_s);
