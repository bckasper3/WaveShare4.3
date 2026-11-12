#include <math.h>
#include <stdio.h>
#include <string.h>
#include "el_ui_fmt.h"
#include "el_time.h"
#include "el_util.h"

/* snprintf wrapper: EL_ERR_SIZE on truncation, output always terminated. */
static el_err_t put(char *buf, size_t cap, int n)
{
    if (n < 0) { buf[0] = '\0'; return EL_ERR_ARG; }
    return ((size_t)n >= cap) ? EL_ERR_SIZE : EL_OK;
}

static el_err_t put_str(char *buf, size_t cap, const char *s)
{
    return el_strlcpy(buf, s, cap) >= cap ? EL_ERR_SIZE : EL_OK;
}

static uint8_t clamp_dec(uint8_t d) { return d > 3 ? 3 : d; }

el_err_t el_ui_fmt_fixed(float value, uint8_t decimals, const char *unit, char *buf, size_t cap)
{
    if (!buf || !cap) return EL_ERR_ARG;
    if (!isfinite(value)) return put_str(buf, cap, EL_UI_NA);
    bool has_unit = unit && unit[0];
    float shown = value;
    /* Avoid "-0.0": a value that rounds to zero is shown unsigned. */
    float half_lsb = 0.5f / powf(10.0f, (float)clamp_dec(decimals));
    if (fabsf(shown) < half_lsb) shown = 0.0f;
    return put(buf, cap, snprintf(buf, cap, "%.*f%s%s", (int)clamp_dec(decimals), (double)shown,
                                  has_unit ? " " : "", has_unit ? unit : ""));
}

el_err_t el_ui_fmt_secs(uint32_t s, char *buf, size_t cap)
{
    if (!buf || !cap) return EL_ERR_ARG;
    unsigned long d = (unsigned long)(s / 86400u);
    unsigned h = (unsigned)((s / 3600u) % 24u), m = (unsigned)((s / 60u) % 60u), sec = (unsigned)(s % 60u);
    if (d) return h ? put(buf, cap, snprintf(buf, cap, "%lu d %u h", d, h))
                    : put(buf, cap, snprintf(buf, cap, "%lu d", d));
    if (h) return m ? put(buf, cap, snprintf(buf, cap, "%u h %u min", h, m))
                    : put(buf, cap, snprintf(buf, cap, "%u h", h));
    if (m) return sec ? put(buf, cap, snprintf(buf, cap, "%u min %u s", m, sec))
                      : put(buf, cap, snprintf(buf, cap, "%u min", m));
    return put(buf, cap, snprintf(buf, cap, "%u s", sec));
}

el_err_t el_ui_fmt_uptime(uint32_t s, char *buf, size_t cap)
{
    if (!buf || !cap) return EL_ERR_ARG;
    unsigned long d = (unsigned long)(s / 86400u);
    unsigned h = (unsigned)((s / 3600u) % 24u), m = (unsigned)((s / 60u) % 60u), sec = (unsigned)(s % 60u);
    if (d) return put(buf, cap, snprintf(buf, cap, "%lu d %02u:%02u:%02u", d, h, m, sec));
    return put(buf, cap, snprintf(buf, cap, "%02u:%02u:%02u", h, m, sec));
}

el_err_t el_ui_fmt_ago(int64_t now_s, int64_t then_s, char *buf, size_t cap)
{
    if (!buf || !cap) return EL_ERR_ARG;
    if (then_s <= 0) return put_str(buf, cap, EL_UI_NA);
    int64_t d = now_s - then_s;
    if (d < 60) return put_str(buf, cap, "just now");
    if (d < 3600) return put(buf, cap, snprintf(buf, cap, "%d min ago", (int)(d / 60)));
    if (d < 86400) return put(buf, cap, snprintf(buf, cap, "%d h ago", (int)(d / 3600)));
    if (d < 60 * 86400) return put(buf, cap, snprintf(buf, cap, "%d d ago", (int)(d / 86400)));
    return put_str(buf, cap, "> 60 d ago");
}

static void local_dt(int64_t unix_s, int16_t off_min, el_datetime_t *dt)
{
    int64_t t = unix_s + (int64_t)off_min * 60;
    el_time_to_datetime(t < 0 ? 0 : t, dt);
}

el_err_t el_ui_fmt_date(int64_t unix_s, int16_t off, uint8_t fmt, char *buf, size_t cap)
{
    if (!buf || !cap) return EL_ERR_ARG;
    el_datetime_t dt;
    local_dt(unix_s, off, &dt);
    int y = dt.year, mo = dt.month, d = dt.day;
    switch (fmt) {
    case EL_DATE_US: return put(buf, cap, snprintf(buf, cap, "%02d/%02d/%04d", mo, d, y));
    case EL_DATE_EU: return put(buf, cap, snprintf(buf, cap, "%02d.%02d.%04d", d, mo, y));
    default:         return put(buf, cap, snprintf(buf, cap, "%04d-%02d-%02d", y, mo, d));
    }
}

el_err_t el_ui_fmt_time(int64_t unix_s, int16_t off, bool seconds, char *buf, size_t cap)
{
    if (!buf || !cap) return EL_ERR_ARG;
    el_datetime_t dt;
    local_dt(unix_s, off, &dt);
    if (seconds)
        return put(buf, cap, snprintf(buf, cap, "%02d:%02d:%02d", dt.hour, dt.minute, dt.second));
    return put(buf, cap, snprintf(buf, cap, "%02d:%02d", dt.hour, dt.minute));
}

el_err_t el_ui_fmt_datetime(int64_t unix_s, int16_t off, uint8_t fmt, char *buf, size_t cap)
{
    if (!buf || !cap) return EL_ERR_ARG;
    char d[16], t[8];
    el_ui_fmt_date(unix_s, off, fmt, d, sizeof d);
    el_ui_fmt_time(unix_s, off, false, t, sizeof t);
    return put(buf, cap, snprintf(buf, cap, "%s %s", d, t));
}

el_err_t el_ui_fmt_short(int64_t unix_s, int16_t off, char *buf, size_t cap)
{
    static const char *const MON[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    if (!buf || !cap) return EL_ERR_ARG;
    el_datetime_t dt;
    local_dt(unix_s, off, &dt);
    const char *m = (dt.month >= 1 && dt.month <= 12) ? MON[dt.month - 1] : "---";
    return put(buf, cap, snprintf(buf, cap, "%s %02d %02d:%02d", m, dt.day, dt.hour, dt.minute));
}

uint32_t el_ui_page_count(uint32_t count, uint32_t page_size)
{
    if (page_size == 0 || count == 0) return 1;
    return (count - 1u) / page_size + 1u;
}

bool el_ui_page_newest_first(uint32_t count, uint32_t page_size, uint32_t page,
                             uint32_t *start_index, uint32_t *n)
{
    if (n) *n = 0;
    if (!page_size || page >= el_ui_page_count(count, page_size) || count == 0) return false;
    uint32_t skipped = page * page_size;                 /* < count, checked above */
    uint32_t left = count - skipped;
    if (start_index) *start_index = count - 1u - skipped;
    if (n) *n = left < page_size ? left : page_size;
    return true;
}
