#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "el_uplink.h"
#include "el_stats.h"
#include "el_time.h"

/* ---------------------------------------------------------------- addresses and credentials */

static bool printable(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] > 0x7E) return false;
    return true;
}

/* Offset of the host after the scheme, 0 if the scheme is not http(s)://. */
static size_t scheme_len(const char *url)
{
    if (strncmp(url, "https://", 8) == 0) return 8;
    if (strncmp(url, "http://", 7) == 0) return 7;
    return 0;
}

bool el_uplink_url_ok(const char *url)
{
    if (!url) return false;
    size_t n = strlen(url);
    if (n == 0) return true;                       /* not set */
    if (n > EL_UPLINK_URL_MAX || !printable(url, n)) return false;
    size_t h = scheme_len(url);
    if (!h) return false;
    for (size_t i = 0; i < n; i++)
        if (url[i] == ' ' || url[i] == '"' || url[i] == '\\' || url[i] == '<' || url[i] == '>') return false;
    size_t host_end = h;
    while (url[host_end] && url[host_end] != '/' && url[host_end] != '?' && url[host_end] != '#') host_end++;
    if (host_end == h) return false;               /* no host */
    for (size_t i = h; i < host_end; i++)
        if (url[i] == '@') return false;           /* no user:password@ in the address */
    if (url[h] == ':' || url[h] == '.') return false;
    return true;
}

bool el_uplink_url_is_https(const char *url)
{
    return url && strncmp(url, "https://", 8) == 0;
}

el_err_t el_uplink_url_host(const char *url, char *host, size_t cap)
{
    if (!host || cap == 0) return EL_ERR_ARG;
    host[0] = '\0';
    if (!url || !url[0] || !el_uplink_url_ok(url)) return EL_ERR_ARG;
    size_t h = scheme_len(url);
    size_t e = h;
    while (url[e] && url[e] != '/' && url[e] != '?' && url[e] != '#' && url[e] != ':') e++;
    if (e - h >= cap) return EL_ERR_SIZE;
    memcpy(host, url + h, e - h);
    host[e - h] = '\0';
    return EL_OK;
}

bool el_uplink_ssid_ok(const char *ssid)
{
    if (!ssid) return false;
    size_t n = strlen(ssid);
    return n <= EL_UPLINK_SSID_MAX && printable(ssid, n);
}

bool el_uplink_password_ok(const char *pass)
{
    if (!pass) return false;
    size_t n = strlen(pass);
    if (n == 0) return true;
    return n >= EL_UPLINK_PASS_MIN && n <= EL_UPLINK_PASS_MAX && printable(pass, n);
}

/* ---------------------------------------------------------------- JSON */

el_err_t el_json_escape(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) return EL_ERR_ARG;
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)(src ? src : ""); *p; p++) {
        char esc[8];
        size_t k;
        if (*p == '"' || *p == '\\') { esc[0] = '\\'; esc[1] = (char)*p; k = 2; }
        else if (*p == '\n') { memcpy(esc, "\\n", 2); k = 2; }
        else if (*p == '\r') { memcpy(esc, "\\r", 2); k = 2; }
        else if (*p == '\t') { memcpy(esc, "\\t", 2); k = 2; }
        else if (*p < 0x20 || *p == 0x7F) { snprintf(esc, sizeof esc, "\\u%04x", (unsigned)*p); k = 6; }
        else { esc[0] = (char)*p; k = 1; }
        if (o + k >= cap) { dst[0] = '\0'; return EL_ERR_SIZE; }
        memcpy(dst + o, esc, k);
        o += k;
    }
    dst[o] = '\0';
    return EL_OK;
}

static void put(el_uplink_body_t *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(el_uplink_body_t *b, const char *fmt, ...)
{
    if (b->failed) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= b->cap - b->len) { b->failed = true; return; }
    b->len += (size_t)n;
}

static void put_str(el_uplink_body_t *b, const char *key, const char *val, bool comma)
{
    char esc[160];                         /* longest value: 19-char IDs, all escaped = 114 */
    if (el_json_escape(esc, sizeof esc, val) != EL_OK) { b->failed = true; return; }
    put(b, "%s\"%s\":\"%s\"", comma ? "," : "", key, esc);
}

static void put_num(el_uplink_body_t *b, const char *key, float v, unsigned dec)
{
    if (!isfinite(v)) put(b, ",\"%s\":null", key);
    else put(b, ",\"%s\":%.*f", key, (int)(dec > 4 ? 4 : dec), (double)v);
}

static unsigned clamp_dec(unsigned d) { return d > 3 ? 3 : d; }

void el_uplink_body_begin(el_uplink_body_t *b, char *buf, size_t cap, const el_uplink_device_t *d, bool test)
{
    memset(b, 0, sizeof *b);
    b->buf = buf;
    b->cap = cap;
    if (!buf || cap < 2 || !d) { b->failed = true; return; }
    buf[0] = '\0';
    put(b, "{\"device\":{");
    put_str(b, "type", "envlogger", false);
    put_str(b, "serial", d->serial ? d->serial : "", true);
    put_str(b, "location", d->location ? d->location : "", true);
    put_str(b, "fw", d->fw ? d->fw : "", true);
    put(b, "},\"test\":%s,\"results\":[", test ? "true" : "false");
}

el_err_t el_uplink_body_add(el_uplink_body_t *b, const el_result_t *r, const el_uplink_device_t *d)
{
    if (!b || !r || !d) return EL_ERR_ARG;
    if (b->failed) return EL_ERR_SIZE;
    if (b->n >= EL_UPLINK_BATCH_MAX) return EL_ERR_RANGE;
    size_t mark = b->len;
    unsigned dec = clamp_dec(d->decimals);
    char ts[32] = "", al[40] = "";
    el_time_format_iso8601(r->timestamp_s, ts, sizeof ts);
    put(b, "%s{\"record\":%lu", b->n ? "," : "", (unsigned long)r->seq);
    put_str(b, "timestamp", ts, true);
    put_str(b, "tag", r->tag, true);
    put_str(b, "operator", r->operator_id, true);
    put_num(b, "temperature_c", r->temp_c, dec);
    put_num(b, "temperature_sd_c", r->temp_sd_c, dec + 1);
    put_num(b, "humidity_pct", r->rh_pct, dec);
    put_num(b, "humidity_sd_pct", r->rh_sd_pct, dec + 1);
    put_num(b, "dew_point_c", r->dew_point_c, dec);
    put(b, ",\"repeats\":%u", (unsigned)r->repeats);
    put_str(b, "status", el_result_status_name(r->status), true);
    el_alarms_format(r->alarms, al, sizeof al);
    put_str(b, "alarms", al, true);
    put_num(b, "limit_t_low_c", r->limits.t_low_c, 1);
    put_num(b, "limit_t_high_c", r->limits.t_high_c, 1);
    put_num(b, "limit_rh_low_pct", r->limits.rh_low_pct, 1);
    put_num(b, "limit_rh_high_pct", r->limits.rh_high_pct, 1);
    put(b, ",\"flags\":[");
    bool first = true;
    static const struct { uint8_t bit; const char *name; } F[] = {
        { EL_RES_FLAG_SIMULATED, "SIM" }, { EL_RES_FLAG_AUTO, "AUTO" }, { EL_RES_FLAG_ADJUSTED, "ADJ" },
    };
    for (size_t i = 0; i < sizeof F / sizeof F[0]; i++)
        if (r->flags & F[i].bit) { put(b, "%s\"%s\"", first ? "" : ",", F[i].name); first = false; }
    put(b, "]}");
    if (b->failed) {                       /* roll back: the body stays valid without this result */
        b->failed = false;
        b->len = mark;
        b->buf[mark] = '\0';
        return EL_ERR_SIZE;
    }
    b->n++;
    return EL_OK;
}

el_err_t el_uplink_body_end(el_uplink_body_t *b, size_t *len)
{
    if (!b) return EL_ERR_ARG;
    put(b, "]}");
    if (b->failed) return EL_ERR_SIZE;
    if (len) *len = b->len;
    return EL_OK;
}

/* ---------------------------------------------------------------- verdicts and timing */

el_uplink_verdict_t el_uplink_classify(int http_status)
{
    if (http_status >= 200 && http_status <= 299) return EL_UPLINK_SENT;
    if (http_status < 100) return EL_UPLINK_RETRY;              /* no HTTP answer */
    if (http_status == 408 || http_status == 429 || http_status >= 500) return EL_UPLINK_RETRY;
    return EL_UPLINK_REJECTED;                                  /* 1xx, 3xx, other 4xx */
}

const char *el_uplink_verdict_name(el_uplink_verdict_t v)
{
    switch (v) {
    case EL_UPLINK_SENT:     return "SENT";
    case EL_UPLINK_RETRY:    return "RETRY";
    case EL_UPLINK_REJECTED: return "REJECTED";
    default:                 return "?";
    }
}

uint32_t el_uplink_backoff_ms(uint16_t failures)
{
    if (failures == 0) return 0;
    uint32_t ms = EL_UPLINK_BACKOFF_MIN_MS;
    for (uint16_t i = 1; i < failures && ms < EL_UPLINK_BACKOFF_MAX_MS; i++) ms *= 2u;
    return ms > EL_UPLINK_BACKOFF_MAX_MS ? EL_UPLINK_BACKOFF_MAX_MS : ms;
}

void el_uplink_sched_reset(el_uplink_sched_t *s)
{
    if (!s) return;
    memset(s, 0, sizeof *s);
    s->last = EL_UPLINK_SENT;
}

bool el_uplink_sched_due(const el_uplink_sched_t *s, uint32_t now_ms)
{
    return el_uplink_sched_wait_ms(s, now_ms) == 0;
}

uint32_t el_uplink_sched_wait_ms(const el_uplink_sched_t *s, uint32_t now_ms)
{
    if (!s || !s->waiting) return 0;
    int32_t d = (int32_t)(s->next_ms - now_ms);           /* wrap-safe */
    return d > 0 ? (uint32_t)d : 0;
}

void el_uplink_sched_done(el_uplink_sched_t *s, el_uplink_verdict_t v, uint32_t now_ms)
{
    if (!s) return;
    s->last = v;
    if (v == EL_UPLINK_SENT) {
        s->failures = 0;
        s->waiting = false;
        return;
    }
    if (s->failures < UINT16_MAX) s->failures++;
    uint32_t wait = v == EL_UPLINK_REJECTED ? EL_UPLINK_REJECT_WAIT_MS : el_uplink_backoff_ms(s->failures);
    s->next_ms = now_ms + wait;
    s->waiting = true;
}
