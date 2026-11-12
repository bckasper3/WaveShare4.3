#pragma once
/* Controller helpers (docs/MEASUREMENT.md section 1, docs/APP.md section 2): building the
 * el_meas_env_t from settings + conditions, filling the firmware-owned result fields,
 * ID validation and the log-append retry policy. Pure C, no clock. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_err.h"
#include "el_result.h"
#include "el_meas.h"
#include "el_settings.h"

/* true if s is printable ASCII (0x20..0x7E) and at most max_chars long. "" is valid. */
bool el_app_id_valid(const char *s, size_t max_chars);

typedef struct {
    bool export_busy;
    bool log_full;
} el_app_conditions_t;

/* env from settings (repeats, alarm limits, calibration offsets) and conditions. Timeout 0 =
 * el_meas default; stuck_detect off. EL_ERR_ARG on NULL env/s. */
el_err_t el_app_meas_env(el_meas_env_t *env, const el_settings_t *s, const el_app_conditions_t *c);

/* The run's identity, snapshotted when the request was accepted. */
typedef struct {
    int64_t     timestamp_s;   /* UTC */
    const char *tag;           /* location setting; NULL = "" */
    const char *operator_id;   /* NULL = "" */
} el_app_result_meta_t;

/* Fills timestamp, tag and operator (truncated to the record's field sizes). Leaves everything
 * el_meas filled untouched; seq is assigned by the log. */
void el_app_result_fill(el_result_t *r, const el_app_result_meta_t *m);

/* ---- Log-append retry (a result is never dropped silently) ----
 * After a failed storage_log_append the controller keeps el_meas in DONE (new measurements are
 * rejected RESULT_PENDING), shows the result marked NOT SAVED, and retries every
 * EL_APP_LOG_RETRY_MS, EL_APP_LOG_RETRIES times. After that it stays pending until the operator
 * discards it (EVT_REQ_DISCARD_RESULT) or a later retry succeeds on request. */
#define EL_APP_LOG_RETRY_MS   5000u
#define EL_APP_LOG_RETRIES    3u

typedef struct {
    bool     active;
    uint8_t  retries;       /* retries done after the first failure */
    uint32_t next_ms;
} el_app_retry_t;

void el_app_retry_start(el_app_retry_t *r, uint32_t now_ms);    /* first append failed */
bool el_app_retry_due(const el_app_retry_t *r, uint32_t now_ms); /* active, retries left, time reached */
void el_app_retry_failed(el_app_retry_t *r, uint32_t now_ms);   /* a retry failed: schedule next */
bool el_app_retry_exhausted(const el_app_retry_t *r);
void el_app_retry_clear(el_app_retry_t *r);
