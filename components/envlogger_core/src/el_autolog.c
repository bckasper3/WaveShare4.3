#include <string.h>
#include "el_autolog.h"

bool el_autolog_interval_ok(uint32_t s)
{
    return s == 0 || (s >= EL_AUTOLOG_MIN_S && s <= EL_AUTOLOG_MAX_S);
}

void el_autolog_init(el_autolog_t *a, uint32_t interval_s, uint32_t now_ms)
{
    if (!a) return;
    memset(a, 0, sizeof *a);
    a->interval_s = el_autolog_interval_ok(interval_s) ? interval_s : 0;
    a->next_ms = now_ms + a->interval_s * 1000u;
}

void el_autolog_set_interval(el_autolog_t *a, uint32_t interval_s, uint32_t now_ms)
{
    if (!a) return;
    uint32_t v = el_autolog_interval_ok(interval_s) ? interval_s : 0;
    if (v != a->interval_s) el_autolog_init(a, v, now_ms);
}

/* Wrap-safe: 24 h (the longest interval) is far below the 24.8-day int32 horizon. */
static int32_t until(const el_autolog_t *a, uint32_t now_ms) { return (int32_t)(a->next_ms - now_ms); }

bool el_autolog_due(const el_autolog_t *a, uint32_t now_ms)
{
    return a && a->interval_s && until(a, now_ms) <= 0;
}

void el_autolog_taken(el_autolog_t *a, uint32_t now_ms)
{
    if (!a || !a->interval_s) return;
    uint32_t step = a->interval_s * 1000u;
    a->next_ms += step;
    if (until(a, now_ms) <= 0) {                     /* slots were missed: skip to the future */
        uint32_t late = now_ms - a->next_ms;
        a->next_ms += (late / step + 1u) * step;
    }
}

uint32_t el_autolog_remaining_s(const el_autolog_t *a, uint32_t now_ms)
{
    if (!a || !a->interval_s) return UINT32_MAX;
    int32_t d = until(a, now_ms);
    return d <= 0 ? 0u : ((uint32_t)d + 999u) / 1000u;
}
