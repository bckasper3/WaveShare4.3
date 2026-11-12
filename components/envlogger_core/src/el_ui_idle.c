#include <stddef.h>
#include "el_ui_idle.h"

void el_ui_idle_init(el_ui_idle_t *t, uint32_t now_ms)
{
    if (!t) return;
    t->last_ms = now_ms;
    t->state = EL_UI_PWR_ON;
}

bool el_ui_idle_activity(el_ui_idle_t *t, uint32_t now_ms)
{
    if (!t) return false;
    bool swallow = (t->state == EL_UI_PWR_OFF);
    t->last_ms = now_ms;
    t->state = EL_UI_PWR_ON;
    return swallow;
}

el_ui_pwr_t el_ui_idle_tick(el_ui_idle_t *t, uint32_t now_ms, uint16_t dim_s, uint16_t off_s,
                            bool keep_awake, bool *changed)
{
    if (changed) *changed = false;
    if (!t) return EL_UI_PWR_ON;
    el_ui_pwr_t old = t->state, next;
    if (keep_awake) {
        t->last_ms = now_ms;
        next = EL_UI_PWR_ON;
    } else {
        uint32_t idle = now_ms - t->last_ms;          /* wrap-safe */
        if (off_s && idle >= (uint32_t)off_s * 1000u)      next = EL_UI_PWR_OFF;
        else if (dim_s && idle >= (uint32_t)dim_s * 1000u) next = EL_UI_PWR_DIM;
        else                                               next = EL_UI_PWR_ON;
        /* Never brighten on our own: only a touch (activity) or keep_awake does that. */
        if (next < old) next = old;
    }
    t->state = next;
    if (changed) *changed = (next != old);
    return next;
}

uint8_t el_ui_idle_backlight_pct(el_ui_pwr_t state, uint8_t brightness_pct)
{
    uint8_t on = brightness_pct > 100 ? 100 : brightness_pct < 10 ? 10 : brightness_pct;
    switch (state) {
    case EL_UI_PWR_DIM: return on < EL_UI_DIM_PCT ? on : (uint8_t)EL_UI_DIM_PCT;
    case EL_UI_PWR_OFF: return 0;
    default:            return on;
    }
}
