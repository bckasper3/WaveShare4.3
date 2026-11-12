#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "el_csv.h"
#include "el_stats.h"
#include "el_time.h"

static const char *const HEADER =
    "timestamp,tag,operator,temperature_c,temperature_sd_c,humidity_pct,humidity_sd_pct,"
    "dew_point_c,repeats,status,alarms,limit_t_low_c,limit_t_high_c,limit_rh_low_pct,"
    "limit_rh_high_pct,device_serial,fw_version,record,flags";

const char *el_csv_header(void) { return HEADER; }

/* Bounded writer: keeps dst NUL-terminated, remembers overflow instead of failing each call. */
typedef struct { char *p; size_t cap, len; bool overflow; } wr_t;

static void wr_ch(wr_t *w, char c)
{
    if (w->len + 1 < w->cap) { w->p[w->len++] = c; w->p[w->len] = '\0'; }
    else w->overflow = true;
}

static void wr_fmt(wr_t *w, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void wr_fmt(wr_t *w, const char *fmt, ...)
{
    if (w->overflow) return;
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(w->p + w->len, w->cap - w->len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= w->cap - w->len) { w->overflow = true; w->p[w->len] = '\0'; }
    else w->len += (size_t)n;
}

static void wr_text(wr_t *w, const char *s)
{
    if (!s) s = "";
    bool quote = strpbrk(s, ",\"\r\n") != NULL;
    bool formula = s[0] && strchr("=+-@\t\r", s[0]) != NULL;
    if (quote) wr_ch(w, '"');
    if (formula) wr_ch(w, '\'');
    for (const char *c = s; *c; c++) {
        if (*c == '"') wr_ch(w, '"');
        wr_ch(w, *c);
    }
    if (quote) wr_ch(w, '"');
}

/* Empty field for NAN (no value). */
static void wr_num(wr_t *w, float v, unsigned decimals)
{
    if (!isfinite(v)) return;
    wr_fmt(w, "%.*f", (int)decimals, (double)v);
}

el_err_t el_csv_escape(char *dst, size_t cap, const char *src)
{
    if (!dst || !cap) return EL_ERR_ARG;
    wr_t w = { dst, cap, 0, false };
    dst[0] = '\0';
    wr_text(&w, src);
    return w.overflow ? EL_ERR_SIZE : EL_OK;
}

el_err_t el_csv_format_result(char *dst, size_t cap, const el_result_t *r, const el_csv_ctx_t *ctx)
{
    if (!dst || !cap || !r || !ctx) return EL_ERR_ARG;
    wr_t w = { dst, cap, 0, false };
    dst[0] = '\0';
    unsigned dec = ctx->decimals > 3 ? 3 : ctx->decimals;

    char ts[24], al[40];
    el_time_format_iso8601(r->timestamp_s, ts, sizeof ts);
    el_alarms_format(r->alarms, al, sizeof al);
    wr_text(&w, ts);                                    wr_ch(&w, ',');
    wr_text(&w, r->tag);                                wr_ch(&w, ',');
    wr_text(&w, r->operator_id);                        wr_ch(&w, ',');
    wr_num(&w, r->temp_c, dec);                         wr_ch(&w, ',');
    wr_num(&w, r->temp_sd_c, dec + 1);                  wr_ch(&w, ',');
    wr_num(&w, r->rh_pct, dec);                         wr_ch(&w, ',');
    wr_num(&w, r->rh_sd_pct, dec + 1);                  wr_ch(&w, ',');
    wr_num(&w, r->dew_point_c, dec);                    wr_ch(&w, ',');
    wr_fmt(&w, "%u", (unsigned)r->repeats);             wr_ch(&w, ',');
    wr_text(&w, el_result_status_name(r->status));      wr_ch(&w, ',');
    wr_text(&w, al);                                    wr_ch(&w, ',');
    wr_num(&w, r->limits.t_low_c, 1);                   wr_ch(&w, ',');
    wr_num(&w, r->limits.t_high_c, 1);                  wr_ch(&w, ',');
    wr_num(&w, r->limits.rh_low_pct, 1);                wr_ch(&w, ',');
    wr_num(&w, r->limits.rh_high_pct, 1);               wr_ch(&w, ',');
    wr_text(&w, ctx->device_serial);                    wr_ch(&w, ',');
    wr_text(&w, ctx->fw_version);                       wr_ch(&w, ',');
    wr_fmt(&w, "%lu", (unsigned long)r->seq);           wr_ch(&w, ',');
    /* flags as words separated by '|', e.g. "SIM|AUTO" */
    static const struct { uint8_t bit; const char *word; } FLAG_WORDS[] = {
        { EL_RES_FLAG_SIMULATED, "SIM" }, { EL_RES_FLAG_AUTO, "AUTO" }, { EL_RES_FLAG_ADJUSTED, "ADJ" },
    };
    bool any = false;
    for (size_t i = 0; i < sizeof FLAG_WORDS / sizeof FLAG_WORDS[0]; i++) {
        if (!(r->flags & FLAG_WORDS[i].bit)) continue;
        wr_fmt(&w, "%s%s", any ? "|" : "", FLAG_WORDS[i].word);
        any = true;
    }
    return w.overflow ? EL_ERR_SIZE : EL_OK;
}
