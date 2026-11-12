#include <math.h>
#include <string.h>
#include "unity.h"
#include "el_meas.h"
#include "el_stats.h"

#define STEP_MS 20u

typedef struct {
    el_sensor_sim_t sim;
    el_meas_t       m;
    el_meas_env_t   env;
    uint32_t        now;
} rig_t;

static el_meas_env_t env_default(uint8_t repeats)
{
    el_meas_env_t e;
    memset(&e, 0, sizeof e);
    e.repeats = repeats;
    e.limits.t_low_c = 15.0f;  e.limits.t_high_c = 30.0f;
    e.limits.rh_low_pct = 20.0f; e.limits.rh_high_pct = 70.0f;
    return e;
}

static void rig_init(rig_t *r, float t, float rh, float noise, uint32_t seed)
{
    memset(r, 0, sizeof *r);
    el_sensor_sim_cfg_t c;
    el_sensor_sim_default_cfg(&c);
    c.temp_c = t; c.rh_pct = rh;
    c.noise_temp_sd = noise; c.noise_rh_sd = noise * 5.0f;
    c.seed = seed;
    TEST_ASSERT_EQUAL(EL_OK, el_sensor_sim_init(&r->sim, &c));
    TEST_ASSERT_EQUAL(EL_OK, el_meas_init(&r->m, el_sensor_sim_driver(&r->sim)));
    r->env = env_default(3);
    r->now = 1000;
}

typedef struct {
    unsigned started, done, fault, cancelled, progress_events, steps;
    bool     monotonic;
    uint8_t  last_progress;
    el_meas_out_t last;
    el_result_t   result;          /* copied from the DONE step */
} trace_t;

static void trace_init(trace_t *t) { memset(t, 0, sizeof *t); t->monotonic = true; }

static void trace_step(rig_t *r, trace_t *t)
{
    el_meas_out_t o;
    el_meas_step(&r->m, r->now, &o);
    r->now += STEP_MS;
    t->steps++;
    if (o.progress_pct < t->last_progress || o.progress_pct > 100) t->monotonic = false;
    t->last_progress = o.progress_pct;
    if (o.events & EL_MEAS_EV_STARTED)   t->started++;
    if (o.events & EL_MEAS_EV_PROGRESS)  t->progress_events++;
    if (o.events & EL_MEAS_EV_DONE)      { t->done++; t->result = o.result; }
    if (o.events & EL_MEAS_EV_FAULT)     t->fault++;
    if (o.events & EL_MEAS_EV_CANCELLED) t->cancelled++;
    TEST_ASSERT_EQUAL(o.busy, o.state == EL_MEAS_MEASURING);
    t->last = o;
}

/* Step until the run is no longer busy (or a step budget runs out). */
static void run_to_end(rig_t *r, trace_t *t)
{
    for (int i = 0; i < 2000; i++) {
        trace_step(r, t);
        if (!t->last.busy) return;
    }
    TEST_FAIL_MESSAGE("run did not finish");
}

static void test_clean_run_averages_and_classifies(void)
{
    rig_t r; rig_init(&r, 22.0f, 45.0f, 0.0f, 1);
    trace_t t; trace_init(&t);
    TEST_ASSERT_EQUAL(EL_MEAS_ACCEPTED, el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &r.env, r.now));
    TEST_ASSERT_TRUE(el_meas_busy(&r.m));
    run_to_end(&r, &t);
    TEST_ASSERT_EQUAL_UINT(1, t.started);
    TEST_ASSERT_EQUAL_UINT(1, t.done);
    TEST_ASSERT_EQUAL_UINT(0, t.fault);
    TEST_ASSERT_TRUE(t.monotonic);
    TEST_ASSERT_TRUE(t.progress_events >= 3);
    TEST_ASSERT_EQUAL(EL_MEAS_DONE, t.last.state);
    TEST_ASSERT_EQUAL_UINT8(100, t.last.progress_pct);
    TEST_ASSERT_EQUAL_UINT8(3, t.last.repeat);
    el_result_t *res = &t.result;
    TEST_ASSERT_EQUAL_FLOAT(22.0f, res->temp_c);
    TEST_ASSERT_EQUAL_FLOAT(45.0f, res->rh_pct);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, res->temp_sd_c);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, el_dew_point_c(22.0f, 45.0f), res->dew_point_c);
    TEST_ASSERT_EQUAL_UINT8(3, res->repeats);
    TEST_ASSERT_EQUAL(EL_RES_NORMAL, res->status);
    TEST_ASSERT_EQUAL_UINT8(0, res->alarms);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, res->limits.t_high_c);
    TEST_ASSERT_EQUAL_UINT8(EL_RES_FLAG_SIMULATED, res->flags);
    TEST_ASSERT_EQUAL_UINT32(0, res->seq);                           /* firmware fills these */
    TEST_ASSERT_EQUAL_STRING("", res->tag);
    TEST_ASSERT_NOT_NULL(el_meas_result(&r.m));
    /* DONE blocks a new request until acked (logged) */
    TEST_ASSERT_EQUAL(EL_MEAS_REJ_RESULT_PENDING, el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &r.env, r.now));
    TEST_ASSERT_TRUE(el_meas_ack(&r.m));
    TEST_ASSERT_FALSE(el_meas_ack(&r.m));
    TEST_ASSERT_NULL(el_meas_result(&r.m));
    TEST_ASSERT_EQUAL(EL_MEAS_IDLE, el_meas_state(&r.m));
}

static void test_noise_gives_sd_and_mean(void)
{
    rig_t r; rig_init(&r, 4.0f, 85.0f, 0.2f, 42);
    r.env = env_default(10);
    r.env.limits.t_low_c = 2.0f; r.env.limits.t_high_c = 8.0f;     /* vaccine fridge */
    r.env.limits.rh_high_pct = 95.0f;
    trace_t t; trace_init(&t);
    el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_SERIAL, &r.env, r.now);
    run_to_end(&r, &t);
    TEST_ASSERT_EQUAL_UINT(1, t.done);
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 4.0f, t.result.temp_c);
    TEST_ASSERT_TRUE(t.result.temp_sd_c > 0.0f && t.result.temp_sd_c < 0.6f);
    TEST_ASSERT_TRUE(t.result.rh_sd_pct > 0.0f);
    TEST_ASSERT_EQUAL(EL_RES_NORMAL, t.result.status);
    TEST_ASSERT_EQUAL(EL_MEAS_SRC_SERIAL, t.last.source);
}

static void test_alarm_and_offsets(void)
{
    rig_t r; rig_init(&r, 29.8f, 69.0f, 0.0f, 1);
    r.env.t_offset_c = 0.5f;            /* reference thermometer reads 0.5 degC higher */
    r.env.rh_offset_pct = 2.0f;
    trace_t t; trace_init(&t);
    el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_AUTO, &r.env, r.now);
    run_to_end(&r, &t);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 30.3f, t.result.temp_c);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 71.0f, t.result.rh_pct);
    TEST_ASSERT_EQUAL(EL_RES_ALARM, t.result.status);
    TEST_ASSERT_EQUAL_UINT8(EL_ALARM_T_HIGH | EL_ALARM_RH_HIGH, t.result.alarms);
    TEST_ASSERT_EQUAL_UINT8(EL_RES_FLAG_SIMULATED | EL_RES_FLAG_AUTO | EL_RES_FLAG_ADJUSTED, t.result.flags);
    el_meas_ack(&r.m);
    /* humidity offset never pushes the result above 100 % */
    el_sensor_sim_set_climate(&r.sim, 20.0f, 99.0f);
    r.env.rh_offset_pct = 5.0f;
    r.env.limits.rh_high_pct = 100.0f;
    trace_init(&t);
    el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &r.env, r.now);
    run_to_end(&r, &t);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, t.result.rh_pct);
    TEST_ASSERT_EQUAL(EL_RES_NORMAL, t.result.status);
}

static void test_request_rejections_in_order(void)
{
    rig_t r; rig_init(&r, 20.0f, 50.0f, 0.0f, 1);
    el_meas_env_t e = r.env;
    TEST_ASSERT_EQUAL(EL_MEAS_REJ_INVALID, el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, NULL, 0));
    e.repeats = 0;
    TEST_ASSERT_EQUAL(EL_MEAS_REJ_INVALID, el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &e, 0));
    e.repeats = EL_MEAS_MAX_REPEATS + 1;
    TEST_ASSERT_EQUAL(EL_MEAS_REJ_INVALID, el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &e, 0));
    e = r.env; e.t_offset_c = 11.0f;
    TEST_ASSERT_EQUAL(EL_MEAS_REJ_INVALID, el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &e, 0));
    e = r.env; e.limits.t_low_c = 40.0f;                       /* low above high */
    TEST_ASSERT_EQUAL(EL_MEAS_REJ_INVALID, el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &e, 0));
    TEST_ASSERT_EQUAL(EL_MEAS_REJ_INVALID, el_meas_request(&r.m, EL_MEAS_REQ_COUNT, EL_MEAS_SRC_UI, &r.env, 0));
    TEST_ASSERT_EQUAL(EL_MEAS_REJ_INVALID, el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_COUNT, &r.env, 0));
    TEST_ASSERT_EQUAL(EL_MEAS_REJ_NOT_ACTIVE, el_meas_request(&r.m, EL_MEAS_REQ_CANCEL, EL_MEAS_SRC_UI, NULL, 0));
    e = r.env; e.export_busy = true; e.log_full = true;
    TEST_ASSERT_EQUAL(EL_MEAS_REJ_EXPORT_BUSY, el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &e, 0));
    e.export_busy = false;
    TEST_ASSERT_EQUAL(EL_MEAS_REJ_LOG_FULL, el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &e, 0));
    TEST_ASSERT_EQUAL(EL_MEAS_ACCEPTED, el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &r.env, 0));
    TEST_ASSERT_EQUAL(EL_MEAS_REJ_BUSY, el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_MODBUS, &r.env, 0));
}

static void test_cancel_aborts_without_result(void)
{
    rig_t r; rig_init(&r, 20.0f, 50.0f, 0.0f, 1);
    trace_t t; trace_init(&t);
    el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &r.env, r.now);
    trace_step(&r, &t);                                   /* conversion started */
    TEST_ASSERT_EQUAL(EL_MEAS_ACCEPTED, el_meas_request(&r.m, EL_MEAS_REQ_CANCEL, EL_MEAS_SRC_UI, NULL, r.now));
    trace_step(&r, &t);
    TEST_ASSERT_EQUAL_UINT(1, t.cancelled);
    TEST_ASSERT_EQUAL_UINT(0, t.done);
    TEST_ASSERT_EQUAL(EL_MEAS_IDLE, t.last.state);
    TEST_ASSERT_EQUAL_UINT32(1, r.sim.n_abort);
}

static void test_sensor_faults_map_and_never_leave_a_result(void)
{
    static const struct { el_sim_fault_t sim; el_meas_fault_t meas; } MAP[] = {
        { EL_SIM_FAULT_NO_ACK,       EL_MEAS_FAULT_SENSOR_NO_ACK },
        { EL_SIM_FAULT_ERR_TIMEOUT,  EL_MEAS_FAULT_SENSOR_TIMEOUT },
        { EL_SIM_FAULT_OUT_OF_RANGE, EL_MEAS_FAULT_SENSOR_OUT_OF_RANGE },
        { EL_SIM_FAULT_CRC,          EL_MEAS_FAULT_SENSOR_CRC },
        { EL_SIM_FAULT_INTERNAL,     EL_MEAS_FAULT_SENSOR_INTERNAL },
        { EL_SIM_FAULT_TIMEOUT,      EL_MEAS_FAULT_WATCHDOG },
    };
    for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++) {
        rig_t r; rig_init(&r, 20.0f, 50.0f, 0.0f, 1);
        el_sim_fault_spec_t f = { MAP[i].sim, 2, false };          /* the second reading fails */
        TEST_ASSERT_EQUAL(EL_OK, el_sensor_sim_inject_fault(&r.sim, &f));
        trace_t t; trace_init(&t);
        el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &r.env, r.now);
        run_to_end(&r, &t);
        TEST_ASSERT_EQUAL_UINT(1, t.fault);
        TEST_ASSERT_EQUAL_UINT(0, t.done);
        TEST_ASSERT_EQUAL(EL_MEAS_FAULT, t.last.state);
        TEST_ASSERT_EQUAL(MAP[i].meas, t.last.fault);
        TEST_ASSERT_NULL(el_meas_result(&r.m));
        /* a new request leaves FAULT (implicit ack) and works */
        trace_init(&t);
        TEST_ASSERT_EQUAL(EL_MEAS_ACCEPTED, el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &r.env, r.now));
        if (MAP[i].sim == EL_SIM_FAULT_TIMEOUT) el_sensor_sim_clear_faults(&r.sim);
        run_to_end(&r, &t);
        TEST_ASSERT_EQUAL_UINT(1, t.done);
    }
}

static void test_watchdog_uses_env_timeout(void)
{
    rig_t r; rig_init(&r, 20.0f, 50.0f, 0.0f, 1);
    el_sim_fault_spec_t hang = { EL_SIM_FAULT_TIMEOUT, 1, true };
    el_sensor_sim_inject_fault(&r.sim, &hang);
    r.env.conversion_timeout_ms = 200;
    trace_t t; trace_init(&t);
    el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &r.env, r.now);
    run_to_end(&r, &t);
    TEST_ASSERT_EQUAL(EL_MEAS_FAULT_WATCHDOG, t.last.fault);
    TEST_ASSERT_TRUE(t.steps * STEP_MS <= 260u);
}

static void test_stuck_detection_only_when_enabled(void)
{
    rig_t r; rig_init(&r, 20.0f, 50.0f, 0.3f, 5);
    r.env.stuck_detect = true;
    trace_t t; trace_init(&t);
    el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &r.env, r.now);
    run_to_end(&r, &t);
    TEST_ASSERT_EQUAL_UINT(1, t.done);                               /* noisy readings differ */
    el_meas_ack(&r.m);
    el_sim_fault_spec_t st = { EL_SIM_FAULT_STUCK, 1, true };      /* from now on frozen */
    el_sensor_sim_inject_fault(&r.sim, &st);
    trace_init(&t);
    el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &r.env, r.now);
    run_to_end(&r, &t);
    TEST_ASSERT_EQUAL(EL_MEAS_FAULT_STUCK, t.last.fault);            /* now all 3 frozen */
    r.env.stuck_detect = false;
    trace_init(&t);
    el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &r.env, r.now);
    run_to_end(&r, &t);
    TEST_ASSERT_EQUAL_UINT(1, t.done);
}

static el_sensor_err_t bad_begin(void *c, uint32_t n) { (void)c; (void)n; return EL_SENSOR_OK; }
static el_sensor_poll_t bad_poll(void *c, uint32_t n, el_sensor_sample_t *s, el_sensor_err_t *e)
{
    (void)c; (void)n; (void)e;
    s->temp_c = NAN; s->rh_pct = 50.0f; s->quality = EL_SAMPLE_VALID;
    return EL_SENSOR_READY;
}
static void bad_abort(void *c) { (void)c; }
static el_err_t bad_info(void *c, char *b, size_t n) { (void)c; (void)b; (void)n; return EL_OK; }
static void bad_caps(void *c, el_sensor_caps_t *o) { (void)c; o->flags = 0; o->typ_conversion_ms = 0; }

static void test_bad_sample_and_missing_driver(void)
{
    static const el_sensor_ops_t OPS = { bad_begin, bad_poll, bad_abort, bad_info, bad_caps };
    el_sensor_t d = { &OPS, NULL };
    rig_t r; rig_init(&r, 20.0f, 50.0f, 0.0f, 1);
    TEST_ASSERT_EQUAL(EL_OK, el_meas_set_sensor(&r.m, d));
    trace_t t; trace_init(&t);
    el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &r.env, r.now);
    run_to_end(&r, &t);
    TEST_ASSERT_EQUAL(EL_MEAS_FAULT_BAD_SAMPLE, t.last.fault);
    el_sensor_t none = { NULL, NULL };
    el_meas_set_sensor(&r.m, none);
    trace_init(&t);
    el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &r.env, r.now);
    run_to_end(&r, &t);
    TEST_ASSERT_EQUAL(EL_MEAS_FAULT_NO_DRIVER, t.last.fault);
    /* swapping the driver mid-run is refused */
    el_meas_set_sensor(&r.m, el_sensor_sim_driver(&r.sim));
    el_meas_request(&r.m, EL_MEAS_REQ_MEASURE, EL_MEAS_SRC_UI, &r.env, r.now);
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_meas_set_sensor(&r.m, none));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_meas_init(NULL, none));
    el_meas_step(NULL, 0, NULL);                                    /* must not crash */
}

static void test_names(void)
{
    TEST_ASSERT_EQUAL_STRING("MEASURING", el_meas_state_name(EL_MEAS_MEASURING));
    TEST_ASSERT_EQUAL_STRING("?", el_meas_state_name(EL_MEAS_STATE_COUNT));
    TEST_ASSERT_EQUAL_STRING("LOG_FULL", el_meas_reject_name(EL_MEAS_REJ_LOG_FULL));
    TEST_ASSERT_EQUAL_STRING("SENSOR_CRC", el_meas_fault_name(EL_MEAS_FAULT_SENSOR_CRC));
    TEST_ASSERT_EQUAL_STRING("AUTO", el_meas_src_name(EL_MEAS_SRC_AUTO));
}

void run_meas_tests(void)
{
    RUN_TEST(test_clean_run_averages_and_classifies);
    RUN_TEST(test_noise_gives_sd_and_mean);
    RUN_TEST(test_alarm_and_offsets);
    RUN_TEST(test_request_rejections_in_order);
    RUN_TEST(test_cancel_aborts_without_result);
    RUN_TEST(test_sensor_faults_map_and_never_leave_a_result);
    RUN_TEST(test_watchdog_uses_env_timeout);
    RUN_TEST(test_stuck_detection_only_when_enabled);
    RUN_TEST(test_bad_sample_and_missing_driver);
    RUN_TEST(test_names);
}
