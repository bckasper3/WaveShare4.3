#pragma once
/* Glue between the device state and the el_modbus engine (docs/PROTOCOLS.md section 3,
 * docs/APP.md section 4): input-register snapshot, holding registers from the settings, the
 * write gate, the order in which accepted writes are applied to the settings, and RTU timing.
 * Pure C, no clock. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_err.h"
#include "el_result.h"
#include "el_modbus.h"
#include "el_settings.h"

/* Everything the input registers show, copied out of app_state by the Modbus task. */
typedef struct {
    bool           has_result;
    el_result_t    last;            /* last result (seq 0 if it could not be logged) */
    uint32_t       result_counter;  /* results produced; reg 3 shows the low 16 bits */
    uint8_t        meas_state;      /* el_meas_state_t, equals reg 6 coding */
    uint8_t        progress_pct;
    bool           sensor_fault;    /* the last run faulted -> flag bit 0 */
    bool           simulated;       /* simulated sensor selected -> flag bit 3 */
    bool           log_near_full;
    bool           export_busy;
    bool           sd_present;
    uint32_t       next_auto_s;     /* el_autolog_remaining_s: UINT32_MAX = off */
    uint32_t       uptime_s;
    uint32_t       log_count;
    uint8_t        log_fill_pct;
    const char    *fw_version;      /* "major.minor.patch[-tag]" */
    const char    *serial;
} el_app_mb_src_t;

void     el_app_mb_inputs(const el_app_mb_src_t *s, el_mb_inputs_t *out);

/* Holding registers: repeats, alarm limits, comms settings and the logging interval. */
void el_app_mb_holding(const el_settings_t *set, el_mb_holding_t *out);

/* Write gate: START while a measurement is running or an export reads the log -> BUSY
 * (exception 06). Everything else is accepted (el_meas still decides). */
el_mb_verdict_t el_app_mb_gate(const el_mb_effects_t *fx, bool meas_busy, bool export_busy);

/* For a low/high pair (old values valid, new values valid): true if HIGH must be written
 * before LOW so that every intermediate settings state keeps low < high. Writing low first
 * leaves (new low, old high); if that breaks the rule, high first leaves (old low, new high),
 * which is then valid because old low < old high <= new low < new high. */
bool el_app_pair_high_first(float old_low, float old_high, float new_low, float new_high);

typedef struct {
    el_set_id_t id;
    char        text[24];
} el_app_set_op_t;

/* Settings writes requested by fx, in a safe order: repeats, the two limit pairs (each ordered
 * with el_app_pair_high_first against cur), address, baud, parity, interval. The command is
 * not a setting and is not included. Returns the number of ops written (<= cap). */
size_t el_app_mb_plan(const el_mb_effects_t *fx, const el_settings_t *cur,
                      el_app_set_op_t *ops, size_t cap);

/* ---- RTU timing (Modbus over serial line v1.02, 2.5.1.1) ----
 * One character = start + 8 data + parity + stop bits = 11 bits in every allowed format
 * (parity none uses 2 stop bits). Frame end = 3.5 character times of silence; above 19200 baud
 * the spec fixes it at 1750 us. */
#define EL_APP_MB_BITS_PER_CHAR  11u
#define EL_APP_MB_T35_FAST_US    1750u
uint32_t el_app_mb_char_us(uint32_t baud);            /* rounded up; 0 baud -> 0 */
uint32_t el_app_mb_t35_us(uint32_t baud);
/* UART RX-timeout threshold in character times (ESP-IDF uart_set_rx_timeout units): the t3.5
 * gap rounded DOWN (a gap inside a frame longer than 1.5 chars is already illegal, while
 * rounding up could merge two frames), clamped to 1..126. 9600/19200 -> 3, 115200 -> 18. */
uint8_t  el_app_mb_rx_tout_chars(uint32_t baud);

/* "1.2.3", "0.1.0-skeleton" -> numbers. Missing parts are 0. EL_ERR_ARG if NULL or not
 * starting with a digit. */
el_err_t el_app_parse_version(const char *s, uint16_t *major, uint16_t *minor, uint16_t *patch);
