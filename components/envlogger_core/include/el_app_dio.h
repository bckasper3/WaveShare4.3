#pragma once
/* Customer digital I/O logic (docs/PROTOCOLS.md section 4, docs/APP.md section 5): DI debounce
 * and edge detection, the DI poll period, and the DO1 alarm-output state. Pure C; the dio task
 * owns the structs (not thread-safe).
 *
 * DI poll cost (docs/HARDWARE.md 2.4): every DI read switches the whole CH422G IO port to input for
 * ~2-5 ms; meanwhile the backlight line floats and is pulled ON. With the screen on this is
 * invisible. With the screen OFF each read is a short backlight flash, so polling at 50 ms would
 * make a dark screen flicker at 20 Hz. Trade-off chosen:
 *   - screen on                      -> EL_APP_DIO_POLL_MS (50 ms)
 *   - screen off, DI0 trigger enabled -> EL_APP_DIO_POLL_MS (the customer's trigger wins;
 *                                        TODO: check on real hardware whether the flash shows)
 *   - screen off, DI0 trigger off     -> EL_APP_DIO_POLL_IDLE_MS (DI is only informational) */
#include <stdbool.h>
#include <stdint.h>
#include "el_result.h"

#define EL_APP_DIO_POLL_MS        50u
#define EL_APP_DIO_POLL_IDLE_MS   1000u
#define EL_APP_DI_DEBOUNCE_N      2u     /* consecutive equal reads to accept a change (50-100 ms) */

uint32_t el_app_dio_period_ms(bool backlight_on, bool di0_trigger_enabled);

/* ---- DI debounce ---- */
typedef struct {
    bool    init;
    bool    stable[2];      /* debounced, true = active */
    bool    cand[2];
    uint8_t cnt[2];
} el_app_di_t;

typedef struct {
    bool    changed;        /* a debounced value changed in this update (or the first update) */
    bool    rise[2];        /* inactive -> active */
    bool    fall[2];
    uint8_t bits;           /* bit0 DI0, bit1 DI1 (EVT_DI_CHANGED arg) */
} el_app_di_ev_t;

void el_app_di_init(el_app_di_t *d);
/* raw: polarity-corrected reads (true = active). The FIRST update adopts the raw values as
 * stable without any edge (a DI0 held active at power-up must not start a measurement) and
 * reports changed = true so the caller publishes the initial state. */
void el_app_di_update(el_app_di_t *d, bool di0, bool di1, el_app_di_ev_t *ev);

/* ---- DO1 alarm output (setting do1_mode, el_do1_mode_t) ----
 *   OFF                      always off
 *   ON_ALARM                 on while the LAST result is ALARM (a limit crossed)
 *   BUSY                     on while a measurement is running (follows meas busy)
 * An alarm follows the last result: a new non-alarming result turns it off. A clear request
 * (UI, Modbus reg 100 = 3) turns it off until the next alarming result. Changing the mode
 * re-evaluates the last result (unless it was cleared). ERROR/NONE status never alarms.
 * Unknown mode values behave as OFF. */
typedef struct {
    uint8_t            mode;
    bool               have_result;
    el_result_status_t last_status;
    bool               cleared;
    bool               alarm;
} el_app_do1_t;

bool el_app_do1_matches(uint8_t mode, el_result_status_t st);
void el_app_do1_init(el_app_do1_t *o, uint8_t mode);
void el_app_do1_set_mode(el_app_do1_t *o, uint8_t mode);
void el_app_do1_result(el_app_do1_t *o, el_result_status_t st);
void el_app_do1_clear(el_app_do1_t *o);
/* The wanted output (true = DO1 on). */
bool el_app_do1_output(const el_app_do1_t *o, bool meas_busy);
