#pragma once
/* PC simulator internals (sim_main.c, sim_backend.c). */
#include <stdbool.h>
#include <stdint.h>
#include "sim_tick.h"
#include "el_meas.h"

int64_t sim_now(void);
void    sim_backlight(uint8_t pct);

void sim_backend_init(void);
void sim_backend_step(void);           /* pretend controller + event dispatch to the UI */

/* Scenario setters for the screenshot script. */
void sim_set_sd(bool present);
/* A finished result with these readings (status from the current alarm limits). */
void sim_set_result(float temp_c, float rh_pct, uint8_t repeats, bool automatic);
void sim_set_fault(el_meas_fault_t f);
void sim_set_measuring(uint8_t pct, uint8_t rep);
void sim_set_theme(bool dark);
void sim_set_export(bool busy, uint8_t pct);
void sim_clear_queue(void);
/* Wi-Fi: state 0 off, 1 not set up, 2 connecting, 3 connected, 4 failed; pending results; error text. */
void sim_set_wifi(int state, uint32_t pending, const char *error);
