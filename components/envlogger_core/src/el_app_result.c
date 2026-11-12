#include <stdio.h>
#include <string.h>
#include "el_app_result.h"
#include "el_util.h"

bool el_app_id_valid(const char *s, size_t max_chars)
{
    if (!s) return false;
    size_t n = 0;
    for (; s[n]; n++) {
        if (n >= max_chars) return false;
        if (s[n] < 0x20 || s[n] > 0x7E) return false;
    }
    return true;
}

el_err_t el_app_meas_env(el_meas_env_t *env, const el_settings_t *s, const el_app_conditions_t *c)
{
    if (!env || !s) return EL_ERR_ARG;
    memset(env, 0, sizeof *env);
    if (c) {
        env->export_busy = c->export_busy;
        env->log_full = c->log_full;
    }
    env->repeats = s->repeats;
    env->stuck_detect = false;
    env->limits = el_settings_limits(s);
    env->t_offset_c = s->temp_offset_c;
    env->rh_offset_pct = s->rh_offset_pct;
    return EL_OK;
}

void el_app_result_fill(el_result_t *r, const el_app_result_meta_t *m)
{
    if (!r || !m) return;
    r->timestamp_s = m->timestamp_s;
    el_strlcpy(r->tag, m->tag ? m->tag : "", sizeof r->tag);
    el_strlcpy(r->operator_id, m->operator_id ? m->operator_id : "", sizeof r->operator_id);
}

void el_app_retry_start(el_app_retry_t *r, uint32_t now_ms)
{
    if (!r) return;
    r->active = true;
    r->retries = 0;
    r->next_ms = now_ms + EL_APP_LOG_RETRY_MS;
}

bool el_app_retry_due(const el_app_retry_t *r, uint32_t now_ms)
{
    /* Wrap-safe comparison on the 32-bit millisecond clock. */
    return r && r->active && r->retries < EL_APP_LOG_RETRIES && (int32_t)(now_ms - r->next_ms) >= 0;
}

void el_app_retry_failed(el_app_retry_t *r, uint32_t now_ms)
{
    if (!r || !r->active) return;
    if (r->retries < UINT8_MAX) r->retries++;
    r->next_ms = now_ms + EL_APP_LOG_RETRY_MS;
}

bool el_app_retry_exhausted(const el_app_retry_t *r)
{
    return r && r->active && r->retries >= EL_APP_LOG_RETRIES;
}

void el_app_retry_clear(el_app_retry_t *r)
{
    if (r) memset(r, 0, sizeof *r);
}
