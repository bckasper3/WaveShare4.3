#include <math.h>
#include <string.h>
#include "unity.h"
#include "el_sensor.h"

static el_sensor_sim_t sim;
static el_sensor_t drv;

static void setup(const el_sensor_sim_cfg_t *cfg)
{
    TEST_ASSERT_EQUAL(EL_OK, el_sensor_sim_init(&sim, cfg));
    drv = el_sensor_sim_driver(&sim);
    TEST_ASSERT_TRUE(el_sensor_valid(&drv));
}

/* Start a conversion at t and poll until it ends; returns the poll verdict. */
static el_sensor_poll_t convert(uint32_t t, el_sensor_sample_t *s, el_sensor_err_t *err)
{
    el_sensor_err_t e = drv.ops->begin_conversion(drv.ctx, t);
    if (e != EL_SENSOR_OK) { *err = e; return EL_SENSOR_ERROR; }
    for (uint32_t dt = 0; dt <= 5000; dt += 10) {
        el_sensor_poll_t p = drv.ops->poll(drv.ctx, t + dt, s, err);
        if (p != EL_SENSOR_PENDING) return p;
    }
    return EL_SENSOR_PENDING;
}

static void test_defaults_and_clean_reading(void)
{
    setup(NULL);
    el_sensor_caps_t c;
    drv.ops->caps(drv.ctx, &c);
    TEST_ASSERT_EQUAL_HEX32(EL_SENSOR_CAP_SIMULATED, c.flags);
    TEST_ASSERT_EQUAL_UINT32(100, c.typ_conversion_ms);
    el_sensor_sample_t s; el_sensor_err_t e;
    TEST_ASSERT_EQUAL(EL_SENSOR_READY, convert(1000, &s, &e));
    TEST_ASSERT_EQUAL_FLOAT(22.5f, s.temp_c);
    TEST_ASSERT_EQUAL_FLOAT(45.0f, s.rh_pct);
    TEST_ASSERT_EQUAL_HEX8(EL_SAMPLE_VALID, s.quality);
}

static void test_timing_and_idle_poll(void)
{
    setup(NULL);
    el_sensor_sample_t s; el_sensor_err_t e;
    TEST_ASSERT_EQUAL(EL_SENSOR_ERROR, drv.ops->poll(drv.ctx, 0, &s, &e));      /* idle */
    TEST_ASSERT_EQUAL(EL_SENSOR_ERR_INTERNAL, e);
    TEST_ASSERT_EQUAL(EL_SENSOR_OK, drv.ops->begin_conversion(drv.ctx, 0xFFFFFFC0u));   /* near wrap */
    TEST_ASSERT_EQUAL(EL_SENSOR_PENDING, drv.ops->poll(drv.ctx, 0xFFFFFFC0u + 99u, &s, &e));
    TEST_ASSERT_EQUAL(EL_SENSOR_READY, drv.ops->poll(drv.ctx, 0xFFFFFFC0u + 100u, &s, &e));
}

static void test_climate_noise_and_clamping(void)
{
    el_sensor_sim_cfg_t c;
    el_sensor_sim_default_cfg(&c);
    c.noise_temp_sd = 0.1f; c.noise_rh_sd = 0.5f; c.seed = 7;
    setup(&c);
    el_sensor_sim_set_climate(&sim, 4.0f, 85.0f);        /* walk-in cooler */
    double ts = 0, hs = 0;
    for (int i = 0; i < 200; i++) {
        el_sensor_sample_t s; el_sensor_err_t e;
        TEST_ASSERT_EQUAL(EL_SENSOR_READY, convert((uint32_t)i * 1000u, &s, &e));
        ts += s.temp_c; hs += s.rh_pct;
        TEST_ASSERT_TRUE(s.rh_pct >= 0.0f && s.rh_pct <= 100.0f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 4.0f, (float)(ts / 200));
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 85.0f, (float)(hs / 200));
    el_sensor_sim_set_climate(&sim, 30.0f, 99.9f);
    el_sensor_sim_set_noise(&sim, 0.0f, 5.0f);
    for (int i = 0; i < 50; i++) {
        el_sensor_sample_t s; el_sensor_err_t e;
        convert(500000u + (uint32_t)i * 1000u, &s, &e);
        TEST_ASSERT_TRUE(s.rh_pct <= 100.0f);            /* clamped like a real sensor */
    }
    el_sensor_sim_set_climate(&sim, NAN, NAN);           /* ignored */
    TEST_ASSERT_EQUAL_FLOAT(30.0f, sim.cfg.temp_c);
}

static void test_same_seed_same_samples(void)
{
    el_sensor_sim_cfg_t c;
    el_sensor_sim_default_cfg(&c);
    c.noise_temp_sd = 0.3f; c.noise_rh_sd = 1.0f; c.seed = 1234;
    float a[5], b[5];
    setup(&c);
    for (int i = 0; i < 5; i++) { el_sensor_sample_t s; el_sensor_err_t e; convert((uint32_t)i * 500u, &s, &e); a[i] = s.temp_c; }
    setup(&c);
    for (int i = 0; i < 5; i++) { el_sensor_sample_t s; el_sensor_err_t e; convert((uint32_t)i * 500u, &s, &e); b[i] = s.temp_c; }
    TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof a);
}

static void test_scripted_faults(void)
{
    setup(NULL);
    el_sensor_sample_t s; el_sensor_err_t e;
    el_sim_fault_spec_t f = { EL_SIM_FAULT_NO_ACK, 2, false };
    TEST_ASSERT_EQUAL(EL_OK, el_sensor_sim_inject_fault(&sim, &f));
    TEST_ASSERT_EQUAL(EL_SENSOR_READY, convert(0, &s, &e));            /* 1st fine */
    TEST_ASSERT_EQUAL(EL_SENSOR_ERROR, convert(1000, &s, &e));         /* 2nd: no ACK */
    TEST_ASSERT_EQUAL(EL_SENSOR_ERR_NO_ACK, e);
    TEST_ASSERT_EQUAL(EL_SENSOR_READY, convert(2000, &s, &e));         /* one-shot consumed */

    static const struct { el_sim_fault_t f; el_sensor_err_t e; } MAP[] = {
        { EL_SIM_FAULT_ERR_TIMEOUT, EL_SENSOR_ERR_TIMEOUT }, { EL_SIM_FAULT_OUT_OF_RANGE, EL_SENSOR_ERR_OUT_OF_RANGE },
        { EL_SIM_FAULT_CRC, EL_SENSOR_ERR_CRC }, { EL_SIM_FAULT_INTERNAL, EL_SENSOR_ERR_INTERNAL },
    };
    for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++) {
        el_sim_fault_spec_t g = { MAP[i].f, 1, false };
        TEST_ASSERT_EQUAL(EL_OK, el_sensor_sim_inject_fault(&sim, &g));
        TEST_ASSERT_EQUAL(EL_SENSOR_ERROR, convert(10000u + (uint32_t)i * 1000u, &s, &e));
        TEST_ASSERT_EQUAL(MAP[i].e, e);
    }
    el_sim_fault_spec_t hang = { EL_SIM_FAULT_TIMEOUT, 1, true };
    el_sensor_sim_inject_fault(&sim, &hang);
    TEST_ASSERT_EQUAL(EL_SENSOR_PENDING, convert(50000, &s, &e));
    drv.ops->abort(drv.ctx);
    TEST_ASSERT_EQUAL(EL_SENSOR_PENDING, convert(60000, &s, &e));      /* permanent */
    el_sensor_sim_clear_faults(&sim);
    TEST_ASSERT_EQUAL(EL_SENSOR_READY, convert(70000, &s, &e));
}

static void test_stuck_and_out_of_range(void)
{
    el_sensor_sim_cfg_t c;
    el_sensor_sim_default_cfg(&c);
    c.noise_temp_sd = 0.5f; c.seed = 99;
    setup(&c);
    el_sensor_sample_t s1, s2; el_sensor_err_t e;
    convert(0, &s1, &e);
    el_sim_fault_spec_t st = { EL_SIM_FAULT_STUCK, 1, true };
    el_sensor_sim_inject_fault(&sim, &st);
    convert(1000, &s2, &e);
    TEST_ASSERT_EQUAL_MEMORY(&s1, &s2, sizeof s1);
    el_sensor_sim_clear_faults(&sim);
    el_sensor_sim_set_climate(&sim, 130.0f, 10.0f);                    /* above the sensor range */
    TEST_ASSERT_EQUAL(EL_SENSOR_ERROR, convert(2000, &s1, &e));
    TEST_ASSERT_EQUAL(EL_SENSOR_ERR_OUT_OF_RANGE, e);
}

static void test_bad_arguments_and_names(void)
{
    el_sensor_sim_cfg_t c;
    el_sensor_sim_default_cfg(&c);
    c.noise_rh_sd = -1.0f;
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_sensor_sim_init(&sim, &c));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_sensor_sim_init(NULL, NULL));
    setup(NULL);
    el_sim_fault_spec_t bad = { EL_SIM_FAULT_NONE, 1, false };
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_sensor_sim_inject_fault(&sim, &bad));
    el_sim_fault_spec_t ok = { EL_SIM_FAULT_CRC, 5, false };
    for (int i = 0; i < EL_SIM_MAX_FAULTS; i++) TEST_ASSERT_EQUAL(EL_OK, el_sensor_sim_inject_fault(&sim, &ok));
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_sensor_sim_inject_fault(&sim, &ok));
    char b[96];
    TEST_ASSERT_EQUAL(EL_OK, drv.ops->info(drv.ctx, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING_LEN("SIM t=22.5", b, 10);
    char tiny[4];
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, drv.ops->info(drv.ctx, tiny, sizeof tiny));
    TEST_ASSERT_EQUAL_STRING("CRC", el_sim_fault_name(EL_SIM_FAULT_CRC));
    TEST_ASSERT_EQUAL_STRING("NONE", el_sim_fault_name((el_sim_fault_t)99));
    TEST_ASSERT_EQUAL_STRING("CRC", el_sensor_err_name(EL_SENSOR_ERR_CRC));
    TEST_ASSERT_EQUAL_STRING("INTERNAL", el_sensor_err_name((el_sensor_err_t)99));
    el_sensor_t none = { NULL, NULL };
    TEST_ASSERT_FALSE(el_sensor_valid(&none));
    TEST_ASSERT_FALSE(el_sensor_valid(NULL));
}

void run_sensor_sim_tests(void)
{
    RUN_TEST(test_defaults_and_clean_reading);
    RUN_TEST(test_timing_and_idle_poll);
    RUN_TEST(test_climate_noise_and_clamping);
    RUN_TEST(test_same_seed_same_samples);
    RUN_TEST(test_scripted_faults);
    RUN_TEST(test_stuck_and_out_of_range);
    RUN_TEST(test_bad_arguments_and_names);
}
