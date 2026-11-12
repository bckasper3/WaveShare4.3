#include <string.h>
#include "el_util.h"
#include "el_err.h"
#include "el_result.h"

size_t el_strlcpy(char *dst, const char *src, size_t cap)
{
    size_t n = strlen(src);
    if (cap) {
        size_t c = n < cap - 1 ? n : cap - 1;
        memcpy(dst, src, c);
        dst[c] = '\0';
    }
    return n;
}

void el_put_u16le(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

void el_put_u32le(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

void el_put_u64le(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

void el_put_f32le(uint8_t *p, float v)
{
    uint32_t u; memcpy(&u, &v, sizeof u);
    el_put_u32le(p, u);
}

uint16_t el_get_u16le(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

uint32_t el_get_u32le(const uint8_t *p)
{
    uint32_t v = 0;
    for (int i = 3; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

uint64_t el_get_u64le(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

float el_get_f32le(const uint8_t *p)
{
    uint32_t u = el_get_u32le(p);
    float v; memcpy(&v, &u, sizeof v);
    return v;
}

const char *el_err_name(el_err_t e)
{
    switch (e) {
    case EL_OK:          return "OK";
    case EL_ERR_ARG:     return "ARG";
    case EL_ERR_SIZE:    return "SIZE";
    case EL_ERR_FORMAT:  return "FORMAT";
    case EL_ERR_VERSION: return "VERSION";
    case EL_ERR_CRC:     return "CRC";
    case EL_ERR_RANGE:   return "RANGE";
    case EL_ERR_DENIED:  return "DENIED";
    case EL_ERR_IO:      return "IO";
    }
    return "UNKNOWN";
}

const char *el_result_status_name(el_result_status_t s)
{
    static const char *const NAMES[EL_RES_STATUS_COUNT] = { "NONE", "NORMAL", "ALARM", "ERROR" };
    return (unsigned)s < EL_RES_STATUS_COUNT ? NAMES[s] : "UNKNOWN";
}
