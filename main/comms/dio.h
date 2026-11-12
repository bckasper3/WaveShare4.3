#pragma once
/* Digital I/O on the board terminal (docs/PROTOCOLS.md section 4, docs/APP.md section 5).
 * DI0 = remote trigger (debounced rising edge -> EVT_REQ_MEASURE from SRC_DI, if the
 * di0_trigger setting is on), DI1 = reserved (published only), DO1 = alarm output per the
 * do1_mode setting. Logic: el_app_dio (envlogger_core). Thread-safe. */
#include <stdbool.h>
#include "esp_err.h"
#include "el_result.h"

esp_err_t dio_start(void);                 /* poll task; reads settings, registers a listener */
void      dio_on_result(el_result_status_t st);
void      dio_set_meas_busy(bool busy);
void      dio_clear_alarm(void);
