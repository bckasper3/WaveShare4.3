#include <stdio.h>
#include "el_time.h"

/* Civil-date algorithms by Howard Hinnant (public domain), proleptic Gregorian, UTC. */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int64_t *y, unsigned *m, unsigned *d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int64_t)yoe + era * 400 + (*m <= 2);
}

static bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static unsigned days_in_month(int y, unsigned m)
{
    static const uint8_t D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return (m == 2 && is_leap(y)) ? 29 : D[m - 1];
}

void el_time_to_datetime(int64_t unix_s, el_datetime_t *out)
{
    if (unix_s < 0) unix_s = 0;
    int64_t days = unix_s / 86400, secs = unix_s % 86400, y;
    unsigned m, d;
    civil_from_days(days, &y, &m, &d);
    out->year   = (int16_t)y;
    out->month  = (uint8_t)m;
    out->day    = (uint8_t)d;
    out->hour   = (uint8_t)(secs / 3600);
    out->minute = (uint8_t)(secs / 60 % 60);
    out->second = (uint8_t)(secs % 60);
}

el_err_t el_datetime_to_time(const el_datetime_t *dt, int64_t *unix_s)
{
    if (!dt || !unix_s) return EL_ERR_ARG;
    if (dt->year < 1970 || dt->year > 9999 || dt->month < 1 || dt->month > 12 ||
        dt->day < 1 || dt->day > days_in_month(dt->year, dt->month) ||
        dt->hour > 23 || dt->minute > 59 || dt->second > 59) return EL_ERR_RANGE;
    *unix_s = days_from_civil(dt->year, dt->month, dt->day) * 86400 +
              dt->hour * 3600 + dt->minute * 60 + dt->second;
    return EL_OK;
}

el_err_t el_time_format_iso8601(int64_t unix_s, char *buf, size_t cap)
{
    if (!buf) return EL_ERR_ARG;
    if (cap < 21) return EL_ERR_SIZE;
    el_datetime_t t; el_time_to_datetime(unix_s, &t);
    snprintf(buf, cap, "%04d-%02u-%02uT%02u:%02u:%02uZ",
             t.year, t.month, t.day, t.hour, t.minute, t.second);
    return EL_OK;
}

el_err_t el_time_format_compact(int64_t unix_s, char *buf, size_t cap)
{
    if (!buf) return EL_ERR_ARG;
    if (cap < 16) return EL_ERR_SIZE;
    el_datetime_t t; el_time_to_datetime(unix_s, &t);
    snprintf(buf, cap, "%04d%02u%02u-%02u%02u%02u",
             t.year, t.month, t.day, t.hour, t.minute, t.second);
    return EL_OK;
}

/* Read exactly n decimal digits. */
static bool digits(const char *s, int n, int *out)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return true;
}

el_err_t el_time_parse_iso8601(const char *s, int64_t *unix_s)
{
    if (!s || !unix_s) return EL_ERR_ARG;
    int y, mo, d, h, mi, se;
    if (!digits(s, 4, &y) || s[4] != '-' || !digits(s + 5, 2, &mo) || s[7] != '-' ||
        !digits(s + 8, 2, &d) || (s[10] != 'T' && s[10] != ' ') ||
        !digits(s + 11, 2, &h) || s[13] != ':' || !digits(s + 14, 2, &mi) || s[16] != ':' ||
        !digits(s + 17, 2, &se)) return EL_ERR_FORMAT;
    const char *end = s + 19;
    if (*end == 'Z') end++;
    if (*end != '\0') return EL_ERR_FORMAT;
    el_datetime_t dt = { (int16_t)y, (uint8_t)mo, (uint8_t)d, (uint8_t)h, (uint8_t)mi, (uint8_t)se };
    return el_datetime_to_time(&dt, unix_s);
}
