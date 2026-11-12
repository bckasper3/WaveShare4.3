#pragma once
/* Measurement statistics, alarm-limit classification and psychrometrics. Pure C. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_err.h"
#include "el_result.h"

typedef struct {
    float mean;
    float sd;           /* sample SD (n-1); 0 when n == 1 */
    float min, max;
} el_stats_t;

/* n >= 1 and every value finite, else EL_ERR_ARG / EL_ERR_RANGE (out untouched). */
el_err_t el_stats_compute(const float *values, size_t n, el_stats_t *out);

/* Sensor ranges (SHT4x/SHT3x): limits and offsets are checked against these. */
#define EL_TEMP_MIN_C    (-40.0f)
#define EL_TEMP_MAX_C    125.0f
#define EL_RH_MIN_PCT    0.0f
#define EL_RH_MAX_PCT    100.0f

/* Every value finite and inside its range, and low < high for both pairs. */
bool el_limits_valid(const el_limits_t *l);

/* NORMAL when inside every limit (a value equal to a limit is inside), ALARM when any limit is
 * crossed (*alarms gets the EL_ALARM_* bits, may be NULL), ERROR when t or rh is not finite.
 * NULL limits -> NORMAL for finite values. */
el_result_status_t el_classify(const el_limits_t *l, float temp_c, float rh_pct, uint8_t *alarms);

/* Dew point (Magnus formula, b = 17.62, c = 243.12 degC; +-0.35 degC for -45..60 degC).
 * NAN when an input is not finite or rh <= 0. */
float el_dew_point_c(float temp_c, float rh_pct);

/* "T_LOW|RH_HIGH" (empty string for 0). EL_ERR_SIZE if truncated. */
el_err_t el_alarms_format(uint8_t alarms, char *buf, size_t cap);
