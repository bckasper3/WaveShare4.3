#pragma once
/* One measurement result: the device's core data object. Everything that stores, shows or
 * transmits a result uses this type. String lengths here ARE the on-flash limits (el_record),
 * so the UI must cap text entry at EL_*_LEN - 1 characters. */
#include <stdint.h>

#define EL_TAG_LEN         20   /* free label: the location setting at the time of the reading */
#define EL_OPERATOR_LEN     8

typedef enum {
    EL_RES_NONE = 0,
    EL_RES_NORMAL,              /* inside every limit */
    EL_RES_ALARM,               /* at least one limit crossed: see alarms */
    EL_RES_ERROR,               /* no valid value */
    EL_RES_STATUS_COUNT
} el_result_status_t;

/* Which limits a result crossed (bit mask, el_result_t.alarms) */
#define EL_ALARM_T_LOW     (1u << 0)
#define EL_ALARM_T_HIGH    (1u << 1)
#define EL_ALARM_RH_LOW    (1u << 2)
#define EL_ALARM_RH_HIGH   (1u << 3)

/* Result flags (bit mask) */
#define EL_RES_FLAG_SIMULATED  (1u << 0)   /* produced by the simulated sensor: never real data */
#define EL_RES_FLAG_AUTO       (1u << 1)   /* taken by the periodic logger, not on request */
#define EL_RES_FLAG_ADJUSTED   (1u << 2)   /* a non-zero calibration offset was applied */

/* Alarm limits. A limit is effectively disabled by setting it to the end of its range
 * (temperature -40 / 125 degC, humidity 0 / 100 %RH): a reading can never cross it. */
typedef struct {
    float t_low_c, t_high_c;
    float rh_low_pct, rh_high_pct;
} el_limits_t;

typedef struct {
    uint32_t seq;                      /* log sequence number, assigned by the datalog */
    int64_t  timestamp_s;              /* Unix time, UTC */
    char     tag[EL_TAG_LEN];
    char     operator_id[EL_OPERATOR_LEN];
    float    temp_c;                   /* mean of the readings, offset applied */
    float    temp_sd_c;                /* sample standard deviation, 0 if repeats < 2 */
    float    rh_pct;                   /* mean relative humidity, offset applied, 0..100 */
    float    rh_sd_pct;
    float    dew_point_c;              /* derived from temp_c and rh_pct (recomputed on decode) */
    uint8_t  repeats;
    el_result_status_t status;
    uint8_t  alarms;                   /* EL_ALARM_* */
    el_limits_t limits;                /* the limits in force when the result was taken */
    uint8_t  flags;                    /* EL_RES_FLAG_* */
} el_result_t;

/* Stable upper-case names used in CSV and on the serial port ("NORMAL", "ALARM", ...). */
const char *el_result_status_name(el_result_status_t s);
