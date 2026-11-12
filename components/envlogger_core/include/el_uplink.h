#pragma once
/* Wi-Fi result upload ("uplink"), the pure-C part (docs/WIFI.md).
 *
 * SEND-ONLY by design: the logger is an HTTP(S) client that POSTs results to one configured
 * server address. It never listens on a port, never acts on what the server sends back (only
 * the HTTP status code is used) and never updates firmware over Wi-Fi.
 *
 * This module has no I/O: it builds the JSON body, validates the server address, turns an
 * HTTP status into a verdict and decides when to try again. main/net/ does the radio and HTTP. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_err.h"
#include "el_result.h"

#define EL_UPLINK_URL_MAX      120    /* server address, characters */
#define EL_UPLINK_TOKEN_MAX    64     /* access key (sent as "Authorization: Bearer <key>") */
#define EL_UPLINK_SSID_MAX     32     /* 802.11 limit */
#define EL_UPLINK_PASS_MIN     8      /* WPA2/WPA3 personal; "" = open network */
#define EL_UPLINK_PASS_MAX     63
#define EL_UPLINK_BATCH_MAX    10     /* results per POST */
#define EL_UPLINK_RESULT_JSON_MAX 640 /* one result object, worst case (all text fields full of escapes) */
#define EL_UPLINK_BODY_MAX     (512 + EL_UPLINK_BATCH_MAX * (EL_UPLINK_RESULT_JSON_MAX + 1))

/* ---- server address ---- */
/* "" (not set) or http:// / https:// + a host, at most EL_UPLINK_URL_MAX printable characters,
 * no spaces, no user:password@ part (keys go in the access key, not the address). */
bool el_uplink_url_ok(const char *url);
bool el_uplink_url_is_https(const char *url);
/* Host part only, for the screen ("api.example.com"). "" when the URL is not valid. */
el_err_t el_uplink_url_host(const char *url, char *host, size_t cap);

/* Wi-Fi credentials as the settings accept them. */
bool el_uplink_ssid_ok(const char *ssid);       /* "" or 1..32 printable characters */
bool el_uplink_password_ok(const char *pass);   /* "" or 8..63 printable characters */

/* ---- JSON body ----
 * { "device": { "type": "envlogger", "serial": ..., "location": ..., "fw": ... },
 *   "test": false,
 *   "results": [ { "record": 1043, "timestamp": "2026-11-02T14:31:05Z", "tag": ..., "operator": ...,
 *                  "temperature_c": 22.4, "temperature_sd_c": 0.02, "humidity_pct": 45.1,
 *                  "humidity_sd_pct": 0.11, "dew_point_c": 9.9 | null, "repeats": 3,
 *                  "status": "NORMAL", "alarms": "T_HIGH|RH_LOW", "limit_t_low_c": 5.0, ...,
 *                  "flags": ["AUTO", ...] }, ... ] }
 * Non-finite numbers are null. Text is JSON-escaped (control characters as \u00XX).
 * Field names never change; new fields are only added. */

typedef struct {
    const char *serial;
    const char *location;
    const char *fw;
    unsigned    decimals;        /* 0..3, as the display / CSV */
} el_uplink_device_t;

typedef struct {
    char   *buf;
    size_t  cap, len;
    uint8_t n;                   /* results added */
    bool    failed;              /* a write did not fit: never send */
} el_uplink_body_t;

/* Start a body in buf (cap >= 256). test = a connection test with no results. */
void     el_uplink_body_begin(el_uplink_body_t *b, char *buf, size_t cap, const el_uplink_device_t *d, bool test);
/* EL_ERR_SIZE when it does not fit (the body is left at the previous result, still valid to end
 * and send), EL_ERR_RANGE when EL_UPLINK_BATCH_MAX results are already in. */
el_err_t el_uplink_body_add(el_uplink_body_t *b, const el_result_t *r, const el_uplink_device_t *d);
/* Closes the JSON. EL_OK and *len = body length, or EL_ERR_SIZE (do not send). */
el_err_t el_uplink_body_end(el_uplink_body_t *b, size_t *len);

/* JSON string escaping (without the quotes). EL_ERR_SIZE if it does not fit (dst holds ""). */
el_err_t el_json_escape(char *dst, size_t cap, const char *src);

/* ---- what the server's answer means ---- */
typedef enum {
    EL_UPLINK_SENT = 0,          /* 2xx: the results are delivered, advance the watermark */
    EL_UPLINK_RETRY,             /* network error, timeout, 408, 429, 5xx: try again later */
    EL_UPLINK_REJECTED           /* other 3xx/4xx: wrong address or key; retry only slowly */
} el_uplink_verdict_t;
/* http_status < 0 = no HTTP answer (DNS, TCP, TLS, timeout). Redirects are not followed. */
el_uplink_verdict_t el_uplink_classify(int http_status);
const char *el_uplink_verdict_name(el_uplink_verdict_t v);

/* ---- when to try (Wi-Fi reconnect and upload share it) ----
 * Back-off after failures: 5 s, 10 s, 20 s ... capped at 10 min; REJECTED waits 30 min.
 * A settings change or a new result after REJECTED does not hurry it; el_uplink_sched_reset does. */
#define EL_UPLINK_BACKOFF_MIN_MS    5000u
#define EL_UPLINK_BACKOFF_MAX_MS    600000u
#define EL_UPLINK_REJECT_WAIT_MS    1800000u

typedef struct {
    uint32_t next_ms;            /* earliest next attempt (monotonic ms clock, wraps) */
    uint16_t failures;           /* consecutive */
    bool     waiting;            /* next_ms is in force */
    el_uplink_verdict_t last;
} el_uplink_sched_t;

void     el_uplink_sched_reset(el_uplink_sched_t *s);
bool     el_uplink_sched_due(const el_uplink_sched_t *s, uint32_t now_ms);
void     el_uplink_sched_done(el_uplink_sched_t *s, el_uplink_verdict_t v, uint32_t now_ms);
uint32_t el_uplink_backoff_ms(uint16_t failures);   /* failures >= 1 */
/* ms until due (0 = now), for sleeping. */
uint32_t el_uplink_sched_wait_ms(const el_uplink_sched_t *s, uint32_t now_ms);
