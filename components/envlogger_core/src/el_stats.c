#include <math.h>
#include <stdio.h>
#include <string.h>
#include "el_stats.h"

el_err_t el_stats_compute(const float *v, size_t n, el_stats_t *out)
{
    if (!v || !out || n == 0) return EL_ERR_ARG;
    double sum = 0;
    float mn = v[0], mx = v[0];
    for (size_t i = 0; i < n; i++) {
        if (!isfinite(v[i])) return EL_ERR_RANGE;
        sum += (double)v[i];
        if (v[i] < mn) mn = v[i];
        if (v[i] > mx) mx = v[i];
    }
    double mean = sum / (double)n, ss = 0;
    for (size_t i = 0; i < n; i++) {
        double d = (double)v[i] - mean;
        ss += d * d;
    }
    out->mean = (float)mean;
    out->sd   = n > 1 ? (float)sqrt(ss / (double)(n - 1)) : 0.0f;
    out->min  = mn;
    out->max  = mx;
    return EL_OK;
}

static bool in(float v, float lo, float hi) { return isfinite(v) && v >= lo && v <= hi; }

bool el_limits_valid(const el_limits_t *l)
{
    if (!l) return false;
    return in(l->t_low_c, EL_TEMP_MIN_C, EL_TEMP_MAX_C) && in(l->t_high_c, EL_TEMP_MIN_C, EL_TEMP_MAX_C) &&
           in(l->rh_low_pct, EL_RH_MIN_PCT, EL_RH_MAX_PCT) && in(l->rh_high_pct, EL_RH_MIN_PCT, EL_RH_MAX_PCT) &&
           l->t_low_c < l->t_high_c && l->rh_low_pct < l->rh_high_pct;
}

el_result_status_t el_classify(const el_limits_t *l, float t, float rh, uint8_t *alarms)
{
    uint8_t a = 0;
    if (alarms) *alarms = 0;
    if (!isfinite(t) || !isfinite(rh)) return EL_RES_ERROR;
    if (l) {
        if (t < l->t_low_c)      a |= EL_ALARM_T_LOW;
        if (t > l->t_high_c)     a |= EL_ALARM_T_HIGH;
        if (rh < l->rh_low_pct)  a |= EL_ALARM_RH_LOW;
        if (rh > l->rh_high_pct) a |= EL_ALARM_RH_HIGH;
    }
    if (alarms) *alarms = a;
    return a ? EL_RES_ALARM : EL_RES_NORMAL;
}

float el_dew_point_c(float t, float rh)
{
    if (!isfinite(t) || !isfinite(rh) || rh <= 0.0f) return NAN;
    const double b = 17.62, c = 243.12;
    double rhc = rh > 100.0f ? 100.0 : (double)rh;
    double g = log(rhc / 100.0) + b * (double)t / (c + (double)t);
    return (float)(c * g / (b - g));
}

el_err_t el_alarms_format(uint8_t alarms, char *buf, size_t cap)
{
    if (!buf || !cap) return EL_ERR_ARG;
    static const struct { uint8_t bit; const char *word; } W[] = {
        { EL_ALARM_T_LOW, "T_LOW" }, { EL_ALARM_T_HIGH, "T_HIGH" },
        { EL_ALARM_RH_LOW, "RH_LOW" }, { EL_ALARM_RH_HIGH, "RH_HIGH" },
    };
    size_t len = 0;
    buf[0] = '\0';
    for (size_t i = 0; i < sizeof W / sizeof W[0]; i++) {
        if (!(alarms & W[i].bit)) continue;
        int n = snprintf(buf + len, cap - len, "%s%s", len ? "|" : "", W[i].word);
        if (n < 0 || (size_t)n >= cap - len) return EL_ERR_SIZE;
        len += (size_t)n;
    }
    return EL_OK;
}
