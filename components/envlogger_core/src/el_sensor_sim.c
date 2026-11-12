#include <math.h>
#include <stdio.h>
#include <string.h>
#include "el_sensor.h"
#include "el_stats.h"

#define SIM_DEFAULT_SEED 0x2545F491u   /* any non-zero value works for xorshift */

const char *el_sim_fault_name(el_sim_fault_t f)
{
    static const char *const N[EL_SIM_FAULT_COUNT] = {
        "NONE", "NO_ACK", "TIMEOUT", "ERR_TIMEOUT", "OUT_OF_RANGE", "CRC", "INTERNAL", "STUCK"
    };
    return (unsigned)f < EL_SIM_FAULT_COUNT ? N[f] : "NONE";
}

void el_sensor_sim_default_cfg(el_sensor_sim_cfg_t *c)
{
    if (!c) return;
    memset(c, 0, sizeof *c);
    c->temp_c        = 22.5f;
    c->rh_pct        = 45.0f;
    c->seed          = SIM_DEFAULT_SEED;
    c->conversion_ms = 100;
}

static bool cfg_ok(const el_sensor_sim_cfg_t *c)
{
    return isfinite(c->temp_c) && isfinite(c->rh_pct) && isfinite(c->noise_temp_sd) &&
           c->noise_temp_sd >= 0.0f && isfinite(c->noise_rh_sd) && c->noise_rh_sd >= 0.0f;
}

el_err_t el_sensor_sim_init(el_sensor_sim_t *sim, const el_sensor_sim_cfg_t *cfg)
{
    if (!sim) return EL_ERR_ARG;
    el_sensor_sim_cfg_t c;
    if (cfg) c = *cfg; else el_sensor_sim_default_cfg(&c);
    if (!cfg_ok(&c)) return EL_ERR_ARG;
    memset(sim, 0, sizeof *sim);
    sim->cfg = c;
    el_sensor_sim_set_seed(sim, c.seed);
    return EL_OK;
}

void el_sensor_sim_set_seed(el_sensor_sim_t *sim, uint32_t seed)
{
    if (!sim) return;
    sim->cfg.seed = seed ? seed : SIM_DEFAULT_SEED;
    sim->rng = sim->cfg.seed;
}

void el_sensor_sim_set_climate(el_sensor_sim_t *sim, float temp_c, float rh_pct)
{
    if (!sim) return;
    if (isfinite(temp_c)) sim->cfg.temp_c = temp_c;
    if (isfinite(rh_pct)) sim->cfg.rh_pct = rh_pct;
}

void el_sensor_sim_set_noise(el_sensor_sim_t *sim, float temp_sd, float rh_sd)
{
    if (!sim) return;
    if (isfinite(temp_sd) && temp_sd >= 0.0f) sim->cfg.noise_temp_sd = temp_sd;
    if (isfinite(rh_sd) && rh_sd >= 0.0f) sim->cfg.noise_rh_sd = rh_sd;
}

void el_sensor_sim_set_timing(el_sensor_sim_t *sim, uint32_t conversion_ms)
{
    if (sim) sim->cfg.conversion_ms = conversion_ms;
}

el_err_t el_sensor_sim_inject_fault(el_sensor_sim_t *sim, const el_sim_fault_spec_t *spec)
{
    if (!sim || !spec || spec->fault <= EL_SIM_FAULT_NONE || spec->fault >= EL_SIM_FAULT_COUNT)
        return EL_ERR_ARG;
    for (size_t i = 0; i < EL_SIM_MAX_FAULTS; i++) {
        if (sim->script[i].used) continue;
        sim->script[i].spec = *spec;
        if (!sim->script[i].spec.nth) sim->script[i].spec.nth = 1;
        sim->script[i].seen = 0;
        sim->script[i].used = true;
        return EL_OK;
    }
    return EL_ERR_SIZE;
}

void el_sensor_sim_clear_faults(el_sensor_sim_t *sim)
{
    if (!sim) return;
    memset(sim->script, 0, sizeof sim->script);
    sim->have_last = false;
}

/* Every armed entry counts this conversion; the first one due fires.
 * A one-shot entry is consumed when it fires; a permanent one stays armed. */
static el_sim_fault_t script_next(el_sensor_sim_t *sim)
{
    el_sim_fault_t hit = EL_SIM_FAULT_NONE;
    for (size_t i = 0; i < EL_SIM_MAX_FAULTS; i++) {
        if (!sim->script[i].used) continue;
        const el_sim_fault_spec_t *s = &sim->script[i].spec;
        uint32_t seen = ++sim->script[i].seen;
        bool due = seen == s->nth || (s->permanent && seen > s->nth);
        if (!due || hit != EL_SIM_FAULT_NONE) continue;
        hit = s->fault;
        if (!s->permanent) sim->script[i].used = false;
    }
    return hit;
}

/* xorshift32: tiny, deterministic, good enough for test noise. */
static uint32_t rng_next(el_sensor_sim_t *sim)
{
    uint32_t x = sim->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    sim->rng = x;
    return x;
}

/* Irwin-Hall: sum of 12 uniforms minus 6 has mean 0, SD 1, range +-6. */
static float gauss(el_sensor_sim_t *sim)
{
    float s = 0.0f;
    for (int i = 0; i < 12; i++) s += (float)(rng_next(sim) >> 8) * (1.0f / 16777216.0f);
    return s - 6.0f;
}

static el_sensor_err_t sim_begin_conversion(void *ctx, uint32_t now_ms)
{
    el_sensor_sim_t *sim = ctx;
    sim->n_conversion++;
    el_sim_fault_t f = script_next(sim);
    sim->busy = false;
    if (f == EL_SIM_FAULT_NO_ACK) return EL_SENSOR_ERR_NO_ACK;
    sim->busy = true;
    sim->op_start_ms = now_ms;
    sim->op_fault = f;
    return EL_SENSOR_OK;
}

static el_sensor_err_t fault_error(el_sim_fault_t f)
{
    switch (f) {
    case EL_SIM_FAULT_ERR_TIMEOUT:   return EL_SENSOR_ERR_TIMEOUT;
    case EL_SIM_FAULT_OUT_OF_RANGE:  return EL_SENSOR_ERR_OUT_OF_RANGE;
    case EL_SIM_FAULT_CRC:           return EL_SENSOR_ERR_CRC;
    case EL_SIM_FAULT_INTERNAL:      return EL_SENSOR_ERR_INTERNAL;
    default:                         return EL_SENSOR_OK;
    }
}

static void make_sample(el_sensor_sim_t *sim, el_sensor_sample_t *s)
{
    el_sensor_sample_t v;
    if (sim->op_fault == EL_SIM_FAULT_STUCK && sim->have_last) {
        v = sim->last;
    } else {
        float nt = sim->cfg.noise_temp_sd > 0.0f ? sim->cfg.noise_temp_sd * gauss(sim) : 0.0f;
        float nh = sim->cfg.noise_rh_sd > 0.0f ? sim->cfg.noise_rh_sd * gauss(sim) : 0.0f;
        v.temp_c = sim->cfg.temp_c + nt;
        v.rh_pct = sim->cfg.rh_pct + nh;
        if (v.rh_pct < EL_RH_MIN_PCT) v.rh_pct = EL_RH_MIN_PCT;     /* a real sensor clamps too */
        if (v.rh_pct > EL_RH_MAX_PCT) v.rh_pct = EL_RH_MAX_PCT;
        v.quality = EL_SAMPLE_VALID;
    }
    sim->last = v;
    sim->have_last = true;
    if (s) *s = v;
}

static el_sensor_poll_t sim_poll(void *ctx, uint32_t now_ms, el_sensor_sample_t *sample, el_sensor_err_t *err)
{
    el_sensor_sim_t *sim = ctx;
    el_sensor_err_t dummy;
    if (!err) err = &dummy;
    *err = EL_SENSOR_OK;
    if (!sim->busy) { *err = EL_SENSOR_ERR_INTERNAL; return EL_SENSOR_ERROR; }
    if (sim->op_fault == EL_SIM_FAULT_TIMEOUT) return EL_SENSOR_PENDING;     /* hangs forever */
    if ((uint32_t)(now_ms - sim->op_start_ms) < sim->cfg.conversion_ms) return EL_SENSOR_PENDING;  /* wrap-safe */

    sim->busy = false;
    el_sensor_err_t e = fault_error(sim->op_fault);
    if (e == EL_SENSOR_OK && (sim->cfg.temp_c < EL_TEMP_MIN_C || sim->cfg.temp_c > EL_TEMP_MAX_C))
        e = EL_SENSOR_ERR_OUT_OF_RANGE;
    if (e != EL_SENSOR_OK) { *err = e; return EL_SENSOR_ERROR; }
    make_sample(sim, sample);
    return EL_SENSOR_READY;
}

static void sim_abort(void *ctx)
{
    el_sensor_sim_t *sim = ctx;
    sim->busy = false;
    sim->n_abort++;
}

static el_err_t sim_info(void *ctx, char *buf, size_t cap)
{
    const el_sensor_sim_t *sim = ctx;
    if (!buf || !cap) return EL_ERR_ARG;
    int n = snprintf(buf, cap, "SIM t=%g C rh=%g %% noise=%g/%g seed=%lu conv=%lums",
                     (double)sim->cfg.temp_c, (double)sim->cfg.rh_pct, (double)sim->cfg.noise_temp_sd,
                     (double)sim->cfg.noise_rh_sd, (unsigned long)sim->cfg.seed,
                     (unsigned long)sim->cfg.conversion_ms);
    return (n < 0 || (size_t)n >= cap) ? EL_ERR_SIZE : EL_OK;
}

static void sim_caps(void *ctx, el_sensor_caps_t *out)
{
    const el_sensor_sim_t *sim = ctx;
    out->flags = EL_SENSOR_CAP_SIMULATED;
    out->typ_conversion_ms = sim->cfg.conversion_ms;
}

static const el_sensor_ops_t SIM_OPS = {
    .begin_conversion = sim_begin_conversion,
    .poll             = sim_poll,
    .abort            = sim_abort,
    .info             = sim_info,
    .caps             = sim_caps,
};

el_sensor_t el_sensor_sim_driver(el_sensor_sim_t *sim)
{
    el_sensor_t s = { &SIM_OPS, sim };
    return s;
}
