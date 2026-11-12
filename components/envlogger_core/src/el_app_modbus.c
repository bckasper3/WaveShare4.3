#include <math.h>
#include <stdio.h>
#include <string.h>
#include "el_app_modbus.h"
#include "el_meas.h"
#include "el_util.h"

static uint16_t clamp_u16(uint32_t v) { return v > UINT16_MAX ? UINT16_MAX : (uint16_t)v; }

void el_app_mb_inputs(const el_app_mb_src_t *s, el_mb_inputs_t *out)
{
    if (!s || !out) return;
    memset(out, 0, sizeof *out);
    if (s->has_result) {
        out->temp_c = s->last.temp_c;
        out->rh_pct = s->last.rh_pct;
        out->dew_point_c = s->last.dew_point_c;
        out->temp_sd_c = s->last.temp_sd_c;
        out->rh_sd_pct = s->last.rh_sd_pct;
        out->status = (uint16_t)s->last.status;
        out->alarms = s->last.alarms;
        out->last_seq = s->last.seq;
        out->last_timestamp_s = s->last.timestamp_s < 0 ? 0u
                              : s->last.timestamp_s > (int64_t)UINT32_MAX ? UINT32_MAX
                              : (uint32_t)s->last.timestamp_s;
        out->repeats = s->last.repeats;
        el_strlcpy(out->last_tag, s->last.tag, sizeof out->last_tag);
    } else {
        out->temp_c = NAN;            /* "no value" */
        out->rh_pct = NAN;
        out->dew_point_c = NAN;
        out->temp_sd_c = NAN;
        out->rh_sd_pct = NAN;
    }
    out->result_counter = (uint16_t)(s->result_counter & 0xFFFFu);
    out->meas_state = s->meas_state;
    out->progress_pct = s->progress_pct;

    uint16_t f = 0;
    if (s->sensor_fault)                 f |= EL_MB_FLAG_SENSOR_FAULT;
    if (s->log_near_full)                f |= EL_MB_FLAG_LOG_NEAR_FULL;
    if (s->export_busy)                  f |= EL_MB_FLAG_EXPORT_BUSY;
    if (s->simulated)                    f |= EL_MB_FLAG_SIMULATED;
    if (s->next_auto_s != UINT32_MAX)    f |= EL_MB_FLAG_AUTOLOG_ON;
    if (s->sd_present)                   f |= EL_MB_FLAG_SD_PRESENT;
    out->flags = f;
    out->next_auto_s = s->next_auto_s;

    uint16_t ma = 0, mi = 0, pa = 0;
    (void)el_app_parse_version(s->fw_version, &ma, &mi, &pa);
    out->fw_major = ma; out->fw_minor = mi; out->fw_patch = pa;
    el_strlcpy(out->serial, s->serial ? s->serial : "", sizeof out->serial);
    out->uptime_s = s->uptime_s;
    out->log_count = s->log_count;
    out->log_used_pct = clamp_u16(s->log_fill_pct);
}

void el_app_mb_holding(const el_settings_t *set, el_mb_holding_t *out)
{
    if (!set || !out) return;
    memset(out, 0, sizeof *out);
    out->repeats = set->repeats;
    out->limits = el_settings_limits(set);
    out->address = set->mb_addr;
    out->baud_code = set->mb_baud;
    out->parity_code = set->mb_parity;
    out->log_interval_s = set->log_interval_s;
}

el_mb_verdict_t el_app_mb_gate(const el_mb_effects_t *fx, bool meas_busy, bool export_busy)
{
    if (!fx || !(fx->mask & EL_MB_FX_COMMAND)) return EL_MB_ACCEPT;
    if (fx->command == EL_MB_CMD_START) return (meas_busy || export_busy) ? EL_MB_REJECT_BUSY : EL_MB_ACCEPT;
    return EL_MB_ACCEPT;
}

bool el_app_pair_high_first(float ol, float oh, float nl, float nh)
{
    (void)ol; (void)nh;
    return !(nl < oh);
}

static size_t add_op(el_app_set_op_t *ops, size_t cap, size_t n, el_set_id_t id, const char *text)
{
    if (n >= cap) return n;
    ops[n].id = id;
    el_strlcpy(ops[n].text, text, sizeof ops[n].text);
    return n + 1;
}

static size_t add_float(el_app_set_op_t *ops, size_t cap, size_t n, el_set_id_t id, float v)
{
    char t[24];
    snprintf(t, sizeof t, "%.7g", (double)v);
    return add_op(ops, cap, n, id, t);
}

static size_t add_enum(el_app_set_op_t *ops, size_t cap, size_t n, el_set_id_t id, uint16_t code)
{
    const el_set_def_t *d = el_settings_def(id);
    if (!d || !d->enum_names || code >= d->enum_count) return n;   /* engine validated; be safe */
    return add_op(ops, cap, n, id, d->enum_names[code]);
}

/* One low/high pair in a safe order. */
static size_t add_pair(el_app_set_op_t *ops, size_t cap, size_t n, bool wl, bool wh,
                       el_set_id_t id_l, el_set_id_t id_h, float ol, float oh, float nl, float nh)
{
    if (wl && wh && el_app_pair_high_first(ol, oh, nl, nh)) {
        n = add_float(ops, cap, n, id_h, nh);
        return add_float(ops, cap, n, id_l, nl);
    }
    if (wl) n = add_float(ops, cap, n, id_l, nl);
    if (wh) n = add_float(ops, cap, n, id_h, nh);
    return n;
}

size_t el_app_mb_plan(const el_mb_effects_t *fx, const el_settings_t *cur,
                      el_app_set_op_t *ops, size_t cap)
{
    if (!fx || !cur || !ops) return 0;
    size_t n = 0;
    char t[24];
    if (fx->mask & EL_MB_FX_REPEATS) {
        snprintf(t, sizeof t, "%u", (unsigned)fx->set.repeats);
        n = add_op(ops, cap, n, EL_SET_REPEATS, t);
    }
    const el_limits_t *l = &fx->set.limits;
    n = add_pair(ops, cap, n, (fx->mask & EL_MB_FX_T_LOW) != 0, (fx->mask & EL_MB_FX_T_HIGH) != 0,
                 EL_SET_T_LOW, EL_SET_T_HIGH, cur->temp_low_c, cur->temp_high_c, l->t_low_c, l->t_high_c);
    n = add_pair(ops, cap, n, (fx->mask & EL_MB_FX_RH_LOW) != 0, (fx->mask & EL_MB_FX_RH_HIGH) != 0,
                 EL_SET_RH_LOW, EL_SET_RH_HIGH, cur->rh_low_pct, cur->rh_high_pct, l->rh_low_pct, l->rh_high_pct);
    if (fx->mask & EL_MB_FX_ADDRESS) {
        snprintf(t, sizeof t, "%u", (unsigned)fx->set.address);
        n = add_op(ops, cap, n, EL_SET_MB_ADDR, t);
    }
    if (fx->mask & EL_MB_FX_BAUD)   n = add_enum(ops, cap, n, EL_SET_MB_BAUD, fx->set.baud_code);
    if (fx->mask & EL_MB_FX_PARITY) n = add_enum(ops, cap, n, EL_SET_MB_PARITY, fx->set.parity_code);
    if (fx->mask & EL_MB_FX_INTERVAL) {
        snprintf(t, sizeof t, "%lu", (unsigned long)fx->set.log_interval_s);
        n = add_op(ops, cap, n, EL_SET_LOG_INTERVAL, t);
    }
    return n;
}

uint32_t el_app_mb_char_us(uint32_t baud)
{
    if (!baud) return 0;
    return (EL_APP_MB_BITS_PER_CHAR * 1000000u + baud - 1u) / baud;
}

uint32_t el_app_mb_t35_us(uint32_t baud)
{
    if (baud > 19200u) return EL_APP_MB_T35_FAST_US;
    return (el_app_mb_char_us(baud) * 7u + 1u) / 2u;
}

uint8_t el_app_mb_rx_tout_chars(uint32_t baud)
{
    uint32_t c = el_app_mb_char_us(baud);
    if (!c) return 3;
    uint32_t n = baud > 19200u ? EL_APP_MB_T35_FAST_US / c : 3u;   /* floor(3.5) = 3 */
    if (n < 1u) n = 1u;
    if (n > 126u) n = 126u;
    return (uint8_t)n;
}

static const char *parse_num(const char *p, uint16_t *out)
{
    uint32_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10u + (uint32_t)(*p - '0');
        if (v > UINT16_MAX) v = UINT16_MAX;
        p++;
    }
    *out = (uint16_t)v;
    return p;
}

el_err_t el_app_parse_version(const char *s, uint16_t *major, uint16_t *minor, uint16_t *patch)
{
    uint16_t v[3] = { 0, 0, 0 };
    if (!s || *s < '0' || *s > '9') {
        if (major) *major = 0;
        if (minor) *minor = 0;
        if (patch) *patch = 0;
        return EL_ERR_ARG;
    }
    const char *p = s;
    for (int i = 0; i < 3; i++) {
        p = parse_num(p, &v[i]);
        if (*p != '.' || p[1] < '0' || p[1] > '9') break;
        p++;
    }
    if (major) *major = v[0];
    if (minor) *minor = v[1];
    if (patch) *patch = v[2];
    return EL_OK;
}
