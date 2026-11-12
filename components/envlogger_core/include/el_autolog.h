#pragma once
/* Periodic automatic logging: decides when the next automatic measurement is due.
 * Pure C, time passed in (monotonic ms, wrap-safe), not thread-safe (the controller owns it).
 *
 * Rules:
 *   - interval 0 = off. Otherwise EL_AUTOLOG_MIN_S..EL_AUTOLOG_MAX_S (10 s .. 24 h).
 *   - The first reading is due one interval after start (or after the interval changes).
 *   - Slots are on a fixed grid (next = previous slot + interval), so readings do not drift by
 *     the measurement time. Slots missed while the device was busy are skipped, not caught up:
 *     after a late reading the next slot is the first grid slot still in the future.
 *   - The controller asks el_autolog_due() when the measurement engine is idle; when a request
 *     is accepted OR rejected it calls el_autolog_taken(), so a refused slot is not retried in a
 *     tight loop (the reason is shown / logged once per slot). */
#include <stdbool.h>
#include <stdint.h>

#define EL_AUTOLOG_MIN_S   10u
#define EL_AUTOLOG_MAX_S   86400u

typedef struct {
    uint32_t interval_s;   /* 0 = off */
    uint32_t next_ms;      /* due time of the next slot */
} el_autolog_t;

/* 0 or MIN..MAX. */
bool     el_autolog_interval_ok(uint32_t interval_s);
/* An invalid interval is treated as off. */
void     el_autolog_init(el_autolog_t *a, uint32_t interval_s, uint32_t now_ms);
/* Restarts the grid only when the interval actually changes. */
void     el_autolog_set_interval(el_autolog_t *a, uint32_t interval_s, uint32_t now_ms);
bool     el_autolog_due(const el_autolog_t *a, uint32_t now_ms);
/* The due slot was used (request accepted or refused): advance to the next future slot. */
void     el_autolog_taken(el_autolog_t *a, uint32_t now_ms);
/* Seconds until the next slot (rounded up; 0 when due). UINT32_MAX when off. */
uint32_t el_autolog_remaining_s(const el_autolog_t *a, uint32_t now_ms);
