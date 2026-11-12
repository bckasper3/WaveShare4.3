#pragma once
/* One self-test table merged from every service (el_app_selftest_merge): board, storage, USB,
 * sensor. Used by the boot self-test (main.c) and the SELFTEST console command. Runs block the
 * caller; not re-entrant (one run at a time, guarded internally). */
#include <stddef.h>
#include <stdint.h>
#include "el_selftest.h"

const el_test_def_t *app_selftest_table(size_t *n);
/* Run every test in the group mask (el_app_selftest_group_mask words). */
el_err_t app_selftest_run_group(uint32_t mask, el_test_sink_fn sink, void *user, el_test_summary_t *sum);
/* Run one test and its dependency chain. EL_ERR_ARG for an unknown id. */
el_err_t app_selftest_run_id(const char *id, el_test_sink_fn sink, void *user, el_test_summary_t *sum);
