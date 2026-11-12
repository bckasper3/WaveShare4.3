#pragma once
/* Sensor driver interface (docs/MEASUREMENT.md section 2).
 * Measurement logic depends only on this ops table. Every operation is NON-BLOCKING:
 * begin_conversion starts a reading and returns at once, poll() is called repeatedly with the
 * current time until it reports READY or ERROR. A driver runs at most one operation at a time.
 * Drivers are not thread-safe; the caller (the measurement task) serialises all calls.
 * Time is always passed in, never read.
 *
 * Drivers here: the simulated sensor (below) and the Sensirion SHT4x / SHT3x (el_sensor_sht.h). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_err.h"

typedef enum {
    EL_SENSOR_OK = 0,
    EL_SENSOR_ERR_NO_ACK,        /* device did not acknowledge on I2C */
    EL_SENSOR_ERR_TIMEOUT,       /* the reading never became available */
    EL_SENSOR_ERR_OUT_OF_RANGE,  /* reading outside the sensor's range */
    EL_SENSOR_ERR_CRC,           /* data CRC-8 mismatch (noise on the bus, bad wiring) */
    EL_SENSOR_ERR_INTERNAL,      /* anything else: driver misuse, unexpected reply */
    EL_SENSOR_ERR_COUNT
} el_sensor_err_t;

typedef enum {
    EL_SENSOR_PENDING = 0,       /* operation still running: poll again later */
    EL_SENSOR_READY,             /* finished; *sample is filled */
    EL_SENSOR_ERROR,             /* finished with an error; *err says which */
} el_sensor_poll_t;

/* Capability flags */
#define EL_SENSOR_CAP_SIMULATED      (1u << 0)   /* synthetic data: results get EL_RES_FLAG_SIMULATED */

/* Sample quality flags */
#define EL_SAMPLE_VALID              (1u << 0)   /* the driver vouches for this reading */

typedef struct {
    float   temp_c;          /* degrees Celsius */
    float   rh_pct;          /* relative humidity, % (0..100) */
    uint8_t quality;         /* EL_SAMPLE_* */
} el_sensor_sample_t;

typedef struct {
    uint32_t flags;               /* EL_SENSOR_CAP_* */
    uint32_t typ_conversion_ms;   /* typical duration, for progress display only (0 = unknown) */
} el_sensor_caps_t;

/* Every op is mandatory.
 * begin_conversion: EL_SENSOR_OK = started; anything else = not started (e.g. NO_ACK).
 * poll: after READY/ERROR the driver is idle again; polling an idle driver returns
 *       ERROR / EL_SENSOR_ERR_INTERNAL.
 * abort: stop whatever is running, leave the driver idle; safe to call when idle.
 * info: one-line description ("SHT4x @0x44"), EL_ERR_SIZE if truncated. */
typedef struct {
    el_sensor_err_t  (*begin_conversion)(void *ctx, uint32_t now_ms);
    el_sensor_poll_t (*poll)(void *ctx, uint32_t now_ms, el_sensor_sample_t *sample, el_sensor_err_t *err);
    void             (*abort)(void *ctx);
    el_err_t         (*info)(void *ctx, char *buf, size_t cap);
    void             (*caps)(void *ctx, el_sensor_caps_t *out);
} el_sensor_ops_t;

/* A driver instance: ops table + the driver's own state (owned by the caller). */
typedef struct {
    const el_sensor_ops_t *ops;
    void                  *ctx;
} el_sensor_t;

/* True if s, s->ops and every op are non-NULL. */
bool        el_sensor_valid(const el_sensor_t *s);
/* Stable upper-case names ("NO_ACK", "TIMEOUT", ...); "INTERNAL" for unknown codes. */
const char *el_sensor_err_name(el_sensor_err_t e);

/* ---------------------------------------------------------------------------------------- */
/* Simulated sensor. Drives the PC UI simulator, the device without a sensor and the tests.
 * Model: reading = true value + noise, noise ~ N(0, sd) from a seedable xorshift PRNG
 * (deterministic: same seed + same calls = same samples). Humidity is clamped to 0..100. */

typedef enum {
    EL_SIM_FAULT_NONE = 0,
    EL_SIM_FAULT_NO_ACK,         /* begin_conversion returns NO_ACK; nothing starts */
    EL_SIM_FAULT_TIMEOUT,        /* conversion starts but never becomes ready (hang) */
    EL_SIM_FAULT_ERR_TIMEOUT,    /* the driver reports its own TIMEOUT error when the op ends */
    EL_SIM_FAULT_OUT_OF_RANGE,   /* op ends with OUT_OF_RANGE */
    EL_SIM_FAULT_CRC,            /* op ends with CRC */
    EL_SIM_FAULT_INTERNAL,       /* op ends with INTERNAL */
    EL_SIM_FAULT_STUCK,          /* conversion repeats the previous sample */
    EL_SIM_FAULT_COUNT
} el_sim_fault_t;

#define EL_SIM_MAX_FAULTS     4

/* "Fire on the nth conversion from now (1 = the next one); if permanent, also on every
 * conversion after that." Conversions are counted at begin_conversion. */
typedef struct {
    el_sim_fault_t fault;
    uint32_t       nth;          /* 0 is treated as 1 */
    bool           permanent;
} el_sim_fault_spec_t;

typedef struct {
    float    temp_c;             /* default 22.5 */
    float    rh_pct;             /* default 45 */
    float    noise_temp_sd;      /* default 0 (degC) */
    float    noise_rh_sd;        /* default 0 (%RH) */
    uint32_t seed;               /* 0 is replaced by a fixed non-zero seed */
    uint32_t conversion_ms;      /* default 100 */
} el_sensor_sim_cfg_t;

/* Simulator state. Treat as opaque; use the functions below. */
typedef struct {
    el_sensor_sim_cfg_t cfg;
    uint32_t            rng;
    bool                busy;
    uint32_t            op_start_ms;
    el_sim_fault_t      op_fault;        /* fault armed for the running op */
    bool                have_last;
    el_sensor_sample_t  last;
    uint32_t            n_conversion, n_abort;
    struct { el_sim_fault_spec_t spec; uint32_t seen; bool used; } script[EL_SIM_MAX_FAULTS];
} el_sensor_sim_t;

void     el_sensor_sim_default_cfg(el_sensor_sim_cfg_t *cfg);
/* cfg NULL = defaults. EL_ERR_ARG if sim is NULL or cfg has non-finite values / negative noise. */
el_err_t el_sensor_sim_init(el_sensor_sim_t *sim, const el_sensor_sim_cfg_t *cfg);
/* The driver handle for el_meas. */
el_sensor_t el_sensor_sim_driver(el_sensor_sim_t *sim);

/* True climate "in the room". Non-finite values are ignored. */
void     el_sensor_sim_set_climate(el_sensor_sim_t *sim, float temp_c, float rh_pct);
void     el_sensor_sim_set_noise(el_sensor_sim_t *sim, float temp_sd, float rh_sd);  /* < 0 or NAN ignored */
void     el_sensor_sim_set_timing(el_sensor_sim_t *sim, uint32_t conversion_ms);
void     el_sensor_sim_set_seed(el_sensor_sim_t *sim, uint32_t seed);
/* EL_ERR_ARG for a bad spec, EL_ERR_SIZE if EL_SIM_MAX_FAULTS entries are already armed. */
el_err_t el_sensor_sim_inject_fault(el_sensor_sim_t *sim, const el_sim_fault_spec_t *spec);
/* Disarms every scripted fault and forgets a stuck value. A running op keeps its fault. */
void     el_sensor_sim_clear_faults(el_sensor_sim_t *sim);
const char *el_sim_fault_name(el_sim_fault_t f);   /* "NO_ACK", "STUCK", ... */
