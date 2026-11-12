#include <stddef.h>
#include <string.h>
#include "el_app_dio.h"
#include "el_settings.h"

uint32_t el_app_dio_period_ms(bool backlight_on, bool di0_trigger_enabled)
{
    return (backlight_on || di0_trigger_enabled) ? EL_APP_DIO_POLL_MS : EL_APP_DIO_POLL_IDLE_MS;
}

void el_app_di_init(el_app_di_t *d)
{
    if (d) memset(d, 0, sizeof *d);
}

void el_app_di_update(el_app_di_t *d, bool di0, bool di1, el_app_di_ev_t *ev)
{
    if (!d || !ev) return;
    memset(ev, 0, sizeof *ev);
    bool raw[2] = { di0, di1 };
    if (!d->init) {
        for (int i = 0; i < 2; i++) { d->stable[i] = d->cand[i] = raw[i]; d->cnt[i] = 0; }
        d->init = true;
        ev->changed = true;
    } else {
        for (int i = 0; i < 2; i++) {
            if (raw[i] == d->stable[i]) { d->cnt[i] = 0; continue; }
            if (d->cnt[i] == 0 || raw[i] != d->cand[i]) { d->cand[i] = raw[i]; d->cnt[i] = 1; }
            else if (d->cnt[i] < UINT8_MAX) d->cnt[i]++;
            if (d->cnt[i] >= EL_APP_DI_DEBOUNCE_N) {
                d->stable[i] = raw[i];
                d->cnt[i] = 0;
                ev->changed = true;
                if (raw[i]) ev->rise[i] = true; else ev->fall[i] = true;
            }
        }
    }
    ev->bits = (uint8_t)((d->stable[0] ? 1u : 0u) | (d->stable[1] ? 2u : 0u));
}

bool el_app_do1_matches(uint8_t mode, el_result_status_t st)
{
    switch (mode) {
    case EL_DO1_ON_ALARM: return st == EL_RES_ALARM;
    default:                            return false;
    }
}

static void reeval(el_app_do1_t *o)
{
    o->alarm = o->have_result && !o->cleared && el_app_do1_matches(o->mode, o->last_status);
}

void el_app_do1_init(el_app_do1_t *o, uint8_t mode)
{
    if (!o) return;
    memset(o, 0, sizeof *o);
    o->mode = mode;
    o->last_status = EL_RES_NONE;
}

void el_app_do1_set_mode(el_app_do1_t *o, uint8_t mode)
{
    if (!o) return;
    o->mode = mode;
    reeval(o);
}

void el_app_do1_result(el_app_do1_t *o, el_result_status_t st)
{
    if (!o) return;
    o->have_result = true;
    o->last_status = st;
    o->cleared = false;
    reeval(o);
}

void el_app_do1_clear(el_app_do1_t *o)
{
    if (!o) return;
    o->cleared = true;
    o->alarm = false;
}

bool el_app_do1_output(const el_app_do1_t *o, bool meas_busy)
{
    if (!o) return false;
    if (o->mode == EL_DO1_BUSY) return meas_busy;
    return o->alarm;
}
