#pragma once
/* Self-test catalogue runner (docs/DIAGNOSTICS.md sections 1 and 7).
 * The firmware supplies a table of tests (each a function that talks to one subsystem);
 * this module runs a selected group in table order, skips tests whose dependency did not
 * pass, judges values against limits, and formats one machine-readable line per test:
 *   TEST <ID> <PASS|WARN|FAIL|SKIP> <value|-> <unit|-> <detail>
 * followed by
 *   SELFTEST END pass=<n> warn=<n> fail=<n> skip=<n>
 * Test IDs are stable once released (PC scripts and production records key on them). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_err.h"

typedef enum { EL_TEST_PASS = 0, EL_TEST_WARN, EL_TEST_FAIL, EL_TEST_SKIP, EL_TEST_OUTCOME_COUNT } el_test_outcome_t;

/* Groups (bit mask): a test may belong to several. */
#define EL_TG_BOOT         (1u << 0)   /* quick, harmless, runs at every power-up (< 1 s total) */
#define EL_TG_FULL         (1u << 1)   /* longer checks; FULL runs include BOOT tests too */
#define EL_TG_FIXTURE      (1u << 2)   /* needs the loopback plug / USB stick */
#define EL_TG_INTERACTIVE  (1u << 3)   /* needs a person to look or touch */
#define EL_TG_ALL          0xFFFFFFFFu

#define EL_SELFTEST_MAX    64          /* tests per table */
#define EL_TEST_DETAIL_LEN 64

typedef struct {
    el_test_outcome_t outcome;
    float             value;                      /* NAN if the test has no single number */
    char              detail[EL_TEST_DETAIL_LEN]; /* short, human readable, no newlines */
} el_test_report_t;

typedef struct {
    const char *id;          /* "I2C_BUS": upper case, no spaces */
    const char *name;        /* "I2C bus and device scan" */
    const char *unit;        /* unit of report.value, "" if none */
    const char *depends_on;  /* id of a test that must PASS or WARN first, or NULL */
    uint32_t    groups;      /* EL_TG_* */
    void      (*run)(void *ctx, el_test_report_t *rep);
} el_test_def_t;

typedef struct { uint16_t count[EL_TEST_OUTCOME_COUNT]; } el_test_summary_t;

/* Called after each test (also for skipped ones). */
typedef void (*el_test_sink_fn)(void *user, const el_test_def_t *t, const el_test_report_t *r);

/* Runs every test whose groups intersect group_mask. A dependency that is in the table but
 * not selected is treated as unknown and does not block. EL_ERR_ARG if the table names an
 * unknown dependency, a dependency that comes later in the table, or a duplicate id. */
el_err_t el_selftest_run(const el_test_def_t *tests, size_t n, uint32_t group_mask,
                         void *ctx, el_test_sink_fn sink, void *user, el_test_summary_t *summary);

/* Validates the table without running anything (use in a unit test of the firmware's table). */
el_err_t el_selftest_validate(const el_test_def_t *tests, size_t n);

/* ---- helpers for test functions ---- */
typedef struct { float fail_lo, warn_lo, warn_hi, fail_hi; } el_test_limits_t;   /* NAN = no limit */

/* FAIL outside [fail_lo, fail_hi], WARN outside [warn_lo, warn_hi], else PASS. NAN value = FAIL. */
el_test_outcome_t el_test_judge(float value, const el_test_limits_t *lim);

/* Fill a report: sets outcome, value and a printf-style detail. */
void el_test_set(el_test_report_t *rep, el_test_outcome_t o, float value, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

const char *el_test_outcome_name(el_test_outcome_t o);
el_err_t    el_selftest_format(char *dst, size_t cap, const el_test_def_t *t, const el_test_report_t *r);
el_err_t    el_selftest_format_summary(char *dst, size_t cap, const el_test_summary_t *s);
