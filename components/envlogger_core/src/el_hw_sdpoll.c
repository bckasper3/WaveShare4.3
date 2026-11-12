#include <stddef.h>
#include "el_hw_sdpoll.h"

void el_hw_sd_poll_init(el_hw_sd_poll_t *p)
{
    if (!p) return;
    p->mounted = false;
    p->fails = 0;
    p->retry_armed = false;
    p->retry_at_ms = 0;
}

el_hw_sd_action_t el_hw_sd_poll_next(const el_hw_sd_poll_t *p, uint32_t now_ms)
{
    if (!p) return EL_HW_SD_ACT_NONE;
    if (p->mounted) return EL_HW_SD_ACT_STATUS;
    /* Wrap-safe "now >= retry_at" (uint32 ms wraps after 49 days of uptime). */
    if (p->retry_armed && (int32_t)(now_ms - p->retry_at_ms) < 0) return EL_HW_SD_ACT_NONE;
    return EL_HW_SD_ACT_MOUNT;
}

bool el_hw_sd_poll_report(el_hw_sd_poll_t *p, el_hw_sd_action_t act, bool ok, uint32_t now_ms)
{
    if (!p) return false;
    switch (act) {
    case EL_HW_SD_ACT_STATUS:
        if (!p->mounted) return false;
        if (ok) { p->fails = 0; return false; }
        if (++p->fails < EL_HW_SD_FAILS_TO_DROP) return false;
        /* Card gone: drop it, and allow an immediate re-probe on the next poll in case it was
         * a glitch (a reinserted card should come back without a 5 s wait). */
        el_hw_sd_poll_set_mounted(p, false, now_ms);
        return true;
    case EL_HW_SD_ACT_MOUNT:
        if (p->mounted) return false;
        if (ok) { el_hw_sd_poll_set_mounted(p, true, now_ms); return true; }
        p->retry_armed = true;
        p->retry_at_ms = now_ms + EL_HW_SD_RETRY_MS;
        return false;
    default:
        return false;
    }
}

void el_hw_sd_poll_set_mounted(el_hw_sd_poll_t *p, bool mounted, uint32_t now_ms)
{
    (void)now_ms;
    if (!p) return;
    p->mounted = mounted;
    p->fails = 0;
    p->retry_armed = false;
}
