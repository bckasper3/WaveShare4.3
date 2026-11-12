#include <stdio.h>
#include <string.h>
#include "el_i2c_check.h"

void el_i2c_scan_clear(el_i2c_scan_t *s) { memset(s, 0, sizeof *s); }

void el_i2c_scan_mark(el_i2c_scan_t *s, uint8_t a)
{
    if (a < 128) s->bits[a >> 3] |= (uint8_t)(1u << (a & 7));
}

bool el_i2c_scan_has(const el_i2c_scan_t *s, uint8_t a)
{
    return a < 128 && (s->bits[a >> 3] >> (a & 7)) & 1u;
}

el_err_t el_i2c_check(const el_i2c_scan_t *scan, const el_i2c_expect_t *ex, size_t n, el_i2c_check_t *out)
{
    if (!scan || !out || (n && !ex) || n > EL_I2C_EXPECT_MAX) return EL_ERR_ARG;
    memset(out, 0, sizeof *out);
    bool missing_required = false, missing_optional = false;

    for (size_t i = 0; i < n; i++) {
        if (ex[i].first > ex[i].last || ex[i].last > 127) return EL_ERR_ARG;
        bool found = false;
        for (unsigned a = ex[i].first; a <= ex[i].last && !found; a++) found = el_i2c_scan_has(scan, (uint8_t)a);
        if (found) out->n_found++;
        else {
            out->missing |= 1u << i;
            if (ex[i].required) missing_required = true; else missing_optional = true;
        }
    }
    for (unsigned a = 0; a < 128; a++) {
        if (!el_i2c_scan_has(scan, (uint8_t)a)) continue;
        bool known = false;
        for (size_t i = 0; i < n && !known; i++) known = a >= ex[i].first && a <= ex[i].last;
        if (known) continue;
        if (out->n_unexpected < EL_I2C_UNEXPECTED_MAX) out->unexpected[out->n_unexpected] = (uint8_t)a;
        out->n_unexpected++;
    }
    /* More unknown ACKs than any real bus carries = electrical fault (e.g. SDA held low). */
    out->outcome = (missing_required || out->n_unexpected > EL_I2C_UNEXPECTED_MAX) ? EL_TEST_FAIL
                 : (missing_optional || out->n_unexpected) ? EL_TEST_WARN : EL_TEST_PASS;
    return EL_OK;
}

void el_i2c_check_describe(const el_i2c_check_t *c, const el_i2c_expect_t *ex, size_t n, char *dst, size_t cap)
{
    if (!cap) return;
    dst[0] = '\0';
    size_t len = 0;
#define ADD(...) do { if (len < cap) { int k_ = snprintf(dst + len, cap - len, __VA_ARGS__); \
                      if (k_ > 0) len += (size_t)k_; } } while (0)
    if (c->outcome == EL_TEST_PASS) { ADD("%u devices OK", (unsigned)c->n_found); return; }
    bool first = true;
    for (size_t i = 0; i < n; i++) {
        if (!(c->missing & (1u << i))) continue;
        ADD("%s%s(0x%02X)%s", first ? "missing " : ", ", ex[i].name, ex[i].first, ex[i].required ? "" : "?");
        first = false;
    }
    if (c->n_unexpected) {
        ADD("%sunexpected", first ? "" : "; ");
        unsigned shown = c->n_unexpected < EL_I2C_UNEXPECTED_MAX ? c->n_unexpected : EL_I2C_UNEXPECTED_MAX;
        for (unsigned i = 0; i < shown; i++) ADD(" 0x%02X", c->unexpected[i]);
        if (c->n_unexpected > shown) ADD(" +%u", (unsigned)(c->n_unexpected - shown));
    }
#undef ADD
}
