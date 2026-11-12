#include <math.h>
#include <string.h>
#include "el_meas.h"
#include "el_stats.h"

#define STUCK_MIN_REPEATS  3     /* fewer identical values are plausible on a quiet, quantised sensor */
#define RUNNING_CAP_PCT    99    /* 100 is reserved for "finished" */

const char *el_meas_state_name(el_meas_state_t s)
{
    static const char *const N[EL_MEAS_STATE_COUNT] = { "IDLE", "MEASURING", "DONE", "FAULT" };
    return (unsigned)s < EL_MEAS_STATE_COUNT ? N[s] : "?";
}

const char *el_meas_reject_name(el_meas_reject_t r)
{
    static const char *const N[EL_MEAS_REJECT_COUNT] = {
        "ACCEPTED", "INVALID", "NOT_ACTIVE", "BUSY", "RESULT_PENDING", "EXPORT_BUSY", "LOG_FULL"
    };
    return (unsigned)r < EL_MEAS_REJECT_COUNT ? N[r] : "?";
}

const char *el_meas_fault_name(el_meas_fault_t f)
{
    static const char *const N[EL_MEAS_FAULT_COUNT] = {
        "NONE", "SENSOR_NO_ACK", "SENSOR_TIMEOUT", "SENSOR_OUT_OF_RANGE", "SENSOR_CRC",
        "SENSOR_INTERNAL", "WATCHDOG", "BAD_SAMPLE", "STUCK", "NO_DRIVER"
    };
    return (unsigned)f < EL_MEAS_FAULT_COUNT ? N[f] : "?";
}

const char *el_meas_src_name(el_meas_src_t s)
{
    static const char *const N[EL_MEAS_SRC_COUNT] = { "UI", "SERIAL", "MODBUS", "DI", "AUTO" };
    return (unsigned)s < EL_MEAS_SRC_COUNT ? N[s] : "?";
}

static bool active(el_meas_state_t s) { return s == EL_MEAS_MEASURING; }

el_err_t el_meas_init(el_meas_t *m, el_sensor_t sensor)
{
    if (!m) return EL_ERR_ARG;
    memset(m, 0, sizeof *m);
    m->sensor = sensor;
    m->state = EL_MEAS_IDLE;
    return EL_OK;
}

el_err_t el_meas_set_sensor(el_meas_t *m, el_sensor_t sensor)
{
    if (!m || active(m->state)) return EL_ERR_ARG;
    m->sensor = sensor;
    return EL_OK;
}

el_meas_state_t el_meas_state(const el_meas_t *m) { return m ? m->state : EL_MEAS_IDLE; }
bool            el_meas_busy(const el_meas_t *m)  { return m && active(m->state); }

const el_result_t *el_meas_result(const el_meas_t *m)
{
    return (m && m->state == EL_MEAS_DONE) ? &m->result : NULL;
}

bool el_meas_ack(el_meas_t *m)
{
    if (!m || (m->state != EL_MEAS_DONE && m->state != EL_MEAS_FAULT)) return false;
    m->state = EL_MEAS_IDLE;
    memset(&m->result, 0, sizeof m->result);
    return true;
}

static bool offsets_ok(const el_meas_env_t *e)
{
    return isfinite(e->t_offset_c) && fabsf(e->t_offset_c) <= EL_MEAS_T_OFFSET_MAX_C &&
           isfinite(e->rh_offset_pct) && fabsf(e->rh_offset_pct) <= EL_MEAS_RH_OFFSET_MAX_PCT;
}

static el_meas_reject_t check(const el_meas_t *m, const el_meas_env_t *env)
{
    if (!env || env->repeats < 1 || env->repeats > EL_MEAS_MAX_REPEATS || !offsets_ok(env) ||
        !el_limits_valid(&env->limits))
        return EL_MEAS_REJ_INVALID;
    if (active(m->state))         return EL_MEAS_REJ_BUSY;
    if (m->state == EL_MEAS_DONE) return EL_MEAS_REJ_RESULT_PENDING;
    if (env->export_busy)         return EL_MEAS_REJ_EXPORT_BUSY;
    if (env->log_full)            return EL_MEAS_REJ_LOG_FULL;
    return EL_MEAS_ACCEPTED;
}

el_meas_reject_t el_meas_request(el_meas_t *m, el_meas_req_t req, el_meas_src_t src,
                                 const el_meas_env_t *env, uint32_t now_ms)
{
    (void)now_ms;
    if (!m || (unsigned)req >= EL_MEAS_REQ_COUNT || (unsigned)src >= EL_MEAS_SRC_COUNT) return EL_MEAS_REJ_INVALID;
    if (req == EL_MEAS_REQ_CANCEL) {
        if (!active(m->state)) return EL_MEAS_REJ_NOT_ACTIVE;
        m->cancel_pending = true;     /* the driver is aborted in the next step, which reports it */
        return EL_MEAS_ACCEPTED;
    }
    el_meas_reject_t r = check(m, env);
    if (r != EL_MEAS_ACCEPTED) return r;

    m->env = *env;
    memset(&m->caps, 0, sizeof m->caps);
    if (el_sensor_valid(&m->sensor)) m->sensor.ops->caps(m->sensor.ctx, &m->caps);
    m->source = src;
    m->state = EL_MEAS_MEASURING;
    m->repeats = env->repeats;
    m->repeat = 0;
    m->progress = 0;
    m->op_running = m->started = m->cancel_pending = false;
    m->fault = EL_MEAS_FAULT_NONE;
    memset(&m->result, 0, sizeof m->result);
    return EL_MEAS_ACCEPTED;
}

/* ---- step helpers: each returns the events it raised ---- */

static el_meas_fault_t map_sensor_err(el_sensor_err_t e)
{
    switch (e) {
    case EL_SENSOR_ERR_NO_ACK:       return EL_MEAS_FAULT_SENSOR_NO_ACK;
    case EL_SENSOR_ERR_TIMEOUT:      return EL_MEAS_FAULT_SENSOR_TIMEOUT;
    case EL_SENSOR_ERR_OUT_OF_RANGE: return EL_MEAS_FAULT_SENSOR_OUT_OF_RANGE;
    case EL_SENSOR_ERR_CRC:          return EL_MEAS_FAULT_SENSOR_CRC;
    default:                         return EL_MEAS_FAULT_SENSOR_INTERNAL;   /* incl. "ERROR with OK" */
    }
}

static void abort_driver(el_meas_t *m)
{
    if (el_sensor_valid(&m->sensor)) m->sensor.ops->abort(m->sensor.ctx);
    m->op_running = false;
}

/* A faulted run never leaves a result behind. */
static uint32_t fail(el_meas_t *m, el_meas_fault_t f)
{
    abort_driver(m);
    m->fault = f;
    m->state = EL_MEAS_FAULT;
    memset(&m->result, 0, sizeof m->result);
    return EL_MEAS_EV_FAULT;
}

static uint32_t op_timeout(const el_meas_t *m)
{
    return m->env.conversion_timeout_ms ? m->env.conversion_timeout_ms : EL_MEAS_CONVERSION_TIMEOUT_MS;
}

/* The readings share 100 %; inside a reading progress creeps toward 95 % of its slice using the
 * driver's typical time. The max() keeps it monotonic whatever the driver does. */
static void update_progress(el_meas_t *m, uint32_t now_ms)
{
    uint32_t typ = m->caps.typ_conversion_ms;
    float frac = 0.0f;
    if (m->op_running && typ > 0) {
        frac = (float)(uint32_t)(now_ms - m->op_start_ms) / (float)typ;
        if (frac > 0.95f) frac = 0.95f;
    }
    uint32_t pct = (uint32_t)(100.0f * ((float)m->repeat + frac) / (float)m->repeats);
    if (pct > RUNNING_CAP_PCT) pct = RUNNING_CAP_PCT;
    if (pct > m->progress) m->progress = (uint8_t)pct;
}

static bool all_identical(const float *a, const float *b, uint8_t n)
{
    for (uint8_t i = 1; i < n; i++)
        if (memcmp(&a[i], &a[0], sizeof a[0]) || memcmp(&b[i], &b[0], sizeof b[0])) return false;
    return true;
}

static uint32_t finish(el_meas_t *m)
{
    if (m->env.stuck_detect && m->repeats >= STUCK_MIN_REPEATS && all_identical(m->temp, m->rh, m->repeats))
        return fail(m, EL_MEAS_FAULT_STUCK);
    el_stats_t t, h;
    if (el_stats_compute(m->temp, m->repeats, &t) != EL_OK ||
        el_stats_compute(m->rh, m->repeats, &h) != EL_OK)       /* cannot happen: values checked */
        return fail(m, EL_MEAS_FAULT_BAD_SAMPLE);

    el_result_t *r = &m->result;
    memset(r, 0, sizeof *r);
    r->temp_c = t.mean;
    r->temp_sd_c = t.sd;
    r->rh_pct = h.mean;
    r->rh_sd_pct = h.sd;
    r->dew_point_c = el_dew_point_c(r->temp_c, r->rh_pct);
    r->repeats = m->repeats;
    r->limits = m->env.limits;
    r->status = el_classify(&m->env.limits, r->temp_c, r->rh_pct, &r->alarms);
    if (m->caps.flags & EL_SENSOR_CAP_SIMULATED) r->flags |= EL_RES_FLAG_SIMULATED;
    if (m->source == EL_MEAS_SRC_AUTO) r->flags |= EL_RES_FLAG_AUTO;
    if (m->env.t_offset_c != 0.0f || m->env.rh_offset_pct != 0.0f) r->flags |= EL_RES_FLAG_ADJUSTED;
    m->state = EL_MEAS_DONE;
    m->progress = 100;
    return EL_MEAS_EV_DONE;
}

static uint32_t on_sample(el_meas_t *m, const el_sensor_sample_t *s)
{
    if (!(s->quality & EL_SAMPLE_VALID) || !isfinite(s->temp_c) || !isfinite(s->rh_pct))
        return fail(m, EL_MEAS_FAULT_BAD_SAMPLE);
    float rh = s->rh_pct + m->env.rh_offset_pct;
    if (rh < EL_RH_MIN_PCT) rh = EL_RH_MIN_PCT;
    if (rh > EL_RH_MAX_PCT) rh = EL_RH_MAX_PCT;
    m->temp[m->repeat] = s->temp_c + m->env.t_offset_c;
    m->rh[m->repeat] = rh;
    m->repeat++;
    return m->repeat >= m->repeats ? finish(m) : 0u;
}

static uint32_t run_step(el_meas_t *m, uint32_t now_ms)
{
    if (m->cancel_pending) {
        m->cancel_pending = false;
        abort_driver(m);
        m->state = EL_MEAS_IDLE;
        return EL_MEAS_EV_CANCELLED;
    }
    uint32_t ev = 0;
    if (!m->started) { m->started = true; ev |= EL_MEAS_EV_STARTED; }
    if (!el_sensor_valid(&m->sensor)) return ev | fail(m, EL_MEAS_FAULT_NO_DRIVER);
    const el_sensor_ops_t *o = m->sensor.ops;

    if (!m->op_running) {
        el_sensor_err_t e = o->begin_conversion(m->sensor.ctx, now_ms);
        if (e != EL_SENSOR_OK) return ev | fail(m, map_sensor_err(e));
        m->op_running = true;
        m->op_start_ms = now_ms;
        return ev;
    }

    el_sensor_sample_t s = { NAN, NAN, 0 };
    el_sensor_err_t err = EL_SENSOR_OK;
    el_sensor_poll_t p = o->poll(m->sensor.ctx, now_ms, &s, &err);
    if (p == EL_SENSOR_PENDING) {
        /* Our watchdog: catches a sensor that never answers (it cannot report its own timeout). */
        if ((uint32_t)(now_ms - m->op_start_ms) >= op_timeout(m)) return ev | fail(m, EL_MEAS_FAULT_WATCHDOG);
        return ev;
    }
    m->op_running = false;
    if (p == EL_SENSOR_READY) return ev | on_sample(m, &s);
    return ev | fail(m, map_sensor_err(err));   /* ERROR, or a driver returning garbage */
}

void el_meas_step(el_meas_t *m, uint32_t now_ms, el_meas_out_t *out)
{
    uint32_t ev = 0;
    if (m && active(m->state)) {
        uint8_t before = m->progress;
        ev = run_step(m, now_ms);
        if (active(m->state)) update_progress(m, now_ms);
        if (m->progress != before && !(ev & (EL_MEAS_EV_CANCELLED | EL_MEAS_EV_FAULT))) ev |= EL_MEAS_EV_PROGRESS;
    }
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->events = ev;
    if (!m) return;
    out->state = m->state;
    out->busy = active(m->state);
    out->source = m->source;
    out->progress_pct = m->progress;
    out->repeat = m->repeat;
    out->repeats = m->repeats;
    out->fault = m->state == EL_MEAS_FAULT ? m->fault : EL_MEAS_FAULT_NONE;
    if (m->state == EL_MEAS_DONE) out->result = m->result;
}
