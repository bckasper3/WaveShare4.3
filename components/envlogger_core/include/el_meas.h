#pragma once
/* Measurement controller: a pure state machine owning one measurement at a time
 * (docs/MEASUREMENT.md has the state table and every rule).
 *
 *   IDLE -> MEASURING x repeats -> DONE --el_meas_ack--> IDLE
 *              any error -> FAULT --el_meas_ack or next accepted request--> IDLE
 *              CANCEL    -> IDLE (event CANCELLED, no result)
 *
 * One measurement = `repeats` sensor readings; the result is their mean (and SD) with the
 * calibration offsets applied, classified against the alarm limits.
 * The firmware's measurement task owns an el_meas_t, forwards requests with el_meas_request()
 * and calls el_meas_step() every ~20-50 ms. Nothing here blocks or reads a clock.
 * Not thread-safe: only the measurement task may touch an el_meas_t. */
#include <stdbool.h>
#include <stdint.h>
#include "el_err.h"
#include "el_result.h"
#include "el_sensor.h"

#define EL_MEAS_MAX_REPEATS              10
#define EL_MEAS_CONVERSION_TIMEOUT_MS    2000u    /* per reading; an SHT answers in < 20 ms */

/* Values match the Modbus "measurement state" register (docs/PROTOCOLS.md). */
typedef enum {
    EL_MEAS_IDLE = 0,
    EL_MEAS_MEASURING,
    EL_MEAS_DONE,          /* result waiting to be logged; leave with el_meas_ack() */
    EL_MEAS_FAULT,         /* last run failed; fault code readable until ack / next request */
    EL_MEAS_STATE_COUNT
} el_meas_state_t;

typedef enum { EL_MEAS_REQ_MEASURE = 0, EL_MEAS_REQ_CANCEL, EL_MEAS_REQ_COUNT } el_meas_req_t;

typedef enum {
    EL_MEAS_SRC_UI = 0, EL_MEAS_SRC_SERIAL, EL_MEAS_SRC_MODBUS, EL_MEAS_SRC_DI, EL_MEAS_SRC_AUTO,
    EL_MEAS_SRC_COUNT
} el_meas_src_t;

/* Request verdicts, in the order they are checked (first match wins). */
typedef enum {
    EL_MEAS_ACCEPTED = 0,
    EL_MEAS_REJ_INVALID,            /* bad argument: NULL env, repeats not 1..10, bad enum */
    EL_MEAS_REJ_NOT_ACTIVE,         /* CANCEL with nothing running */
    EL_MEAS_REJ_BUSY,               /* MEASURE while measuring */
    EL_MEAS_REJ_RESULT_PENDING,     /* DONE: the previous result has not been acked (logged) yet */
    EL_MEAS_REJ_EXPORT_BUSY,        /* a CSV export is reading the log */
    EL_MEAS_REJ_LOG_FULL,           /* the result could not be stored */
    EL_MEAS_REJECT_COUNT
} el_meas_reject_t;

typedef enum {
    EL_MEAS_FAULT_NONE = 0,
    EL_MEAS_FAULT_SENSOR_NO_ACK,        /* sensor error codes, as reported by the driver */
    EL_MEAS_FAULT_SENSOR_TIMEOUT,
    EL_MEAS_FAULT_SENSOR_OUT_OF_RANGE,
    EL_MEAS_FAULT_SENSOR_CRC,
    EL_MEAS_FAULT_SENSOR_INTERNAL,
    EL_MEAS_FAULT_WATCHDOG,             /* our own timeout: the sensor never became ready */
    EL_MEAS_FAULT_BAD_SAMPLE,           /* READY but a value not finite or EL_SAMPLE_VALID clear */
    EL_MEAS_FAULT_STUCK,                /* every reading bit-identical (only if env.stuck_detect) */
    EL_MEAS_FAULT_NO_DRIVER,            /* sensor ops table missing or incomplete */
    EL_MEAS_FAULT_COUNT
} el_meas_fault_t;

/* Events (bit mask) raised by one el_meas_step() call. Several can be set at once. */
#define EL_MEAS_EV_STARTED    (1u << 0)   /* the first driver operation of a run began */
#define EL_MEAS_EV_PROGRESS   (1u << 1)   /* progress_pct changed */
#define EL_MEAS_EV_DONE       (1u << 2)   /* finished: out.result is valid in this step */
#define EL_MEAS_EV_FAULT      (1u << 3)   /* run failed: out.fault; never a result */
#define EL_MEAS_EV_CANCELLED  (1u << 4)   /* run cancelled: no result */

/* Calibration offsets (Admin): added to every reading. */
#define EL_MEAS_T_OFFSET_MAX_C      10.0f
#define EL_MEAS_RH_OFFSET_MAX_PCT   20.0f

/* Conditions and settings, supplied by the firmware with every request. The whole struct is
 * copied when a request is accepted, so settings changed mid-run do not affect that run. */
typedef struct {
    /* conditions */
    bool        export_busy;
    bool        log_full;
    /* settings */
    uint8_t     repeats;               /* 1..EL_MEAS_MAX_REPEATS */
    uint32_t    conversion_timeout_ms; /* 0 = EL_MEAS_CONVERSION_TIMEOUT_MS (per reading) */
    bool        stuck_detect;          /* fault when >= 3 readings are bit-identical; default off */
    el_limits_t limits;                /* el_classify */
    float       t_offset_c;            /* calibration offsets, added to each reading */
    float       rh_offset_pct;         /* humidity result is clamped to 0..100 afterwards */
} el_meas_env_t;

/* Everything the firmware needs after one step. */
typedef struct {
    el_meas_state_t state;
    bool            busy;           /* MEASURING: drives DO1 in "measuring-busy" mode */
    el_meas_src_t   source;         /* who requested the current / last run */
    uint8_t         progress_pct;   /* 0..100, never decreases within a run, 100 on DONE */
    uint8_t         repeat;         /* readings completed so far */
    uint8_t         repeats;        /* readings planned for this run */
    uint32_t        events;         /* EL_MEAS_EV_* raised by this step */
    el_meas_fault_t fault;          /* valid while state == FAULT (and in the FAULT event step) */
    /* Valid while state == DONE (first time in the step that raised EL_MEAS_EV_DONE), else zero.
     * Filled here: temp/rh means and SDs, dew point, repeats, status + alarms (el_classify),
     * limits, flags (SIMULATED, AUTO, ADJUSTED). Left zero for the firmware: seq, timestamp_s,
     * tag, operator_id. */
    el_result_t     result;
} el_meas_out_t;

/* Controller state. Treat as opaque. */
typedef struct {
    el_sensor_t      sensor;
    el_sensor_caps_t caps;
    el_meas_env_t    env;           /* snapshot taken at request acceptance */
    el_meas_state_t  state;
    el_meas_src_t    source;
    bool             op_running;    /* driver operation begun and not yet finished */
    bool             started;       /* STARTED event already raised for this run */
    bool             cancel_pending;
    uint32_t         op_start_ms;
    uint8_t          repeats, repeat;
    uint8_t          progress;
    float            temp[EL_MEAS_MAX_REPEATS];
    float            rh[EL_MEAS_MAX_REPEATS];
    el_meas_fault_t  fault;
    el_result_t      result;
} el_meas_t;

/* EL_ERR_ARG if m is NULL. An incomplete driver is accepted here; runs then fault NO_DRIVER. */
el_err_t el_meas_init(el_meas_t *m, el_sensor_t sensor);
/* Swap the driver (simulation on/off). EL_ERR_ARG if m is NULL or a run is active. */
el_err_t el_meas_set_sensor(el_meas_t *m, el_sensor_t sensor);

/* MEASURE: env required. CANCEL: env may be NULL. Accepted MEASUREs leave FAULT (implicit ack).
 * The run starts at the next el_meas_step(); busy is true immediately. */
el_meas_reject_t el_meas_request(el_meas_t *m, el_meas_req_t req, el_meas_src_t src,
                                 const el_meas_env_t *env, uint32_t now_ms);

/* Advance the state machine. out is fully written every call. Calls the driver at most twice. */
void el_meas_step(el_meas_t *m, uint32_t now_ms, el_meas_out_t *out);

/* Leave DONE or FAULT for IDLE (call after the result is logged / the fault shown).
 * Returns true if the state changed. */
bool el_meas_ack(el_meas_t *m);
/* The pending result while DONE, else NULL. Same data as out.result in the DONE step. */
const el_result_t *el_meas_result(const el_meas_t *m);

el_meas_state_t el_meas_state(const el_meas_t *m);
bool            el_meas_busy(const el_meas_t *m);

/* Stable upper-case names for serial / logs. */
const char *el_meas_state_name(el_meas_state_t s);     /* "IDLE", "MEASURING", ... */
const char *el_meas_reject_name(el_meas_reject_t r);   /* "ACCEPTED", "BUSY", ... */
const char *el_meas_fault_name(el_meas_fault_t f);     /* "NONE", "SENSOR_NO_ACK", ... */
const char *el_meas_src_name(el_meas_src_t s);         /* "UI", "SERIAL", ... */
