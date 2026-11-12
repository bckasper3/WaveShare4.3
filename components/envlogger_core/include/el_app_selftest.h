#pragma once
/* Self-test aggregation across services (docs/DIAGNOSTICS.md sections 1 and 7). Every service
 * exports its own el_test_def_t table; el_selftest_run needs ONE table because dependencies
 * cross tables (SENSOR depends on the board's I2C_BUS). These helpers merge the tables, select
 * a test with its dependency chain, map group words and format the SELFTEST LIST lines. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_err.h"
#include "el_selftest.h"

typedef struct {
    const el_test_def_t *defs;     /* may be NULL (service has no tests / not linked) */
    size_t               n;
} el_app_test_table_t;

typedef struct {
    uint16_t duplicates;           /* ids dropped because an earlier table had them */
    uint16_t deps_cleared;         /* depends_on naming no EARLIER test: cleared, test runs unguarded */
    uint16_t invalid;              /* entries without id or run function: dropped */
    bool     truncated;            /* more tests than cap */
} el_app_merge_report_t;

/* Copies the tables in order into out. First occurrence of an id wins. EL_OK, EL_ERR_ARG
 * (NULL out / n_out), EL_ERR_SIZE (cap reached; the first cap tests are kept). rep may be NULL.
 * The result always passes el_selftest_validate(). */
el_err_t el_app_selftest_merge(const el_app_test_table_t *tables, size_t ntables,
                               el_test_def_t *out, size_t cap, size_t *n_out,
                               el_app_merge_report_t *rep);

/* Index of id (case-insensitive) in t, or -1. */
int el_app_selftest_find(const el_test_def_t *t, size_t n, const char *id);

/* The test named id plus its whole dependency chain, in table order, for "SELFTEST <id>"
 * (run the selection with EL_TG_ALL). EL_ERR_ARG unknown id / NULL, EL_ERR_SIZE cap too small. */
el_err_t el_app_selftest_select(const el_test_def_t *t, size_t n, const char *id,
                                el_test_def_t *out, size_t cap, size_t *n_out);

/* "BOOT" -> BOOT, "FULL" -> BOOT|FULL (a full run includes the quick tests), "FIXTURE",
 * "INTERACTIVE"; case-insensitive. 0 if word is not a group name. */
uint32_t el_app_selftest_group_mask(const char *word);

/* "TESTDEF <id> <groups> <depends|-> <name>", groups as "BOOT|FULL". EL_ERR_SIZE if truncated. */
el_err_t el_app_selftest_format_def(char *dst, size_t cap, const el_test_def_t *t);
