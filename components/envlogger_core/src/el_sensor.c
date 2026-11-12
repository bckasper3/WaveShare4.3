#include "el_sensor.h"

bool el_sensor_valid(const el_sensor_t *s)
{
    if (!s || !s->ops) return false;
    const el_sensor_ops_t *o = s->ops;
    return o->begin_conversion && o->poll && o->abort && o->info && o->caps;
}

const char *el_sensor_err_name(el_sensor_err_t e)
{
    static const char *const N[EL_SENSOR_ERR_COUNT] = {
        "OK", "NO_ACK", "TIMEOUT", "OUT_OF_RANGE", "CRC", "INTERNAL"
    };
    return (unsigned)e < EL_SENSOR_ERR_COUNT ? N[e] : "INTERNAL";
}
