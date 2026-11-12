#include <math.h>
#include <stdio.h>
#include <string.h>
#include "el_sensor_sht.h"

static const uint8_t SHT4X_MEAS[]  = { 0xFD };
static const uint8_t SHT3X_MEAS[]  = { 0x24, 0x00 };
static const uint8_t SHT4X_RESET[] = { 0x94 };
static const uint8_t SHT3X_RESET[] = { 0x30, 0xA2 };

const char *el_sht_model_name(el_sht_model_t m)
{
    return m == EL_SHT3X ? "SHT3x" : m == EL_SHT4X ? "SHT4x" : "?";
}

uint8_t el_sht_crc8(const uint8_t *d, size_t n)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) crc = (uint8_t)((crc & 0x80u) ? (uint8_t)(crc << 1) ^ 0x31u : (uint8_t)(crc << 1));
    }
    return crc;
}

float el_sht_temp_c(uint16_t raw) { return -45.0f + 175.0f * (float)raw / 65535.0f; }

float el_sht_rh_pct(el_sht_model_t m, uint16_t raw)
{
    float rh = m == EL_SHT3X ? 100.0f * (float)raw / 65535.0f : -6.0f + 125.0f * (float)raw / 65535.0f;
    /* SHT4x datasheet: values outside 0..100 are physically impossible and must be cropped. */
    if (rh < 0.0f) rh = 0.0f;
    if (rh > 100.0f) rh = 100.0f;
    return rh;
}

el_sensor_err_t el_sht_decode(el_sht_model_t m, const uint8_t b[6], float *t, float *rh)
{
    if (!b) return EL_SENSOR_ERR_INTERNAL;
    if (el_sht_crc8(b, 2) != b[2] || el_sht_crc8(b + 3, 2) != b[5]) return EL_SENSOR_ERR_CRC;
    if (t)  *t  = el_sht_temp_c((uint16_t)((b[0] << 8) | b[1]));
    if (rh) *rh = el_sht_rh_pct(m, (uint16_t)((b[3] << 8) | b[4]));
    return EL_SENSOR_OK;
}

const uint8_t *el_sht_meas_cmd(el_sht_model_t m, size_t *len)
{
    if (m == EL_SHT3X) { if (len) *len = sizeof SHT3X_MEAS; return SHT3X_MEAS; }
    if (len) *len = sizeof SHT4X_MEAS;
    return SHT4X_MEAS;
}

const uint8_t *el_sht_reset_cmd(el_sht_model_t m, size_t *len)
{
    if (m == EL_SHT3X) { if (len) *len = sizeof SHT3X_RESET; return SHT3X_RESET; }
    if (len) *len = sizeof SHT4X_RESET;
    return SHT4X_RESET;
}

el_err_t el_sht_init(el_sht_t *s, el_sht_model_t model, uint8_t addr7, const el_sht_io_t *io)
{
    if (!s || (unsigned)model >= EL_SHT_MODEL_COUNT || !io || !io->write || !io->read) return EL_ERR_ARG;
    if (addr7 != EL_SHT_ADDR_DEFAULT && addr7 != EL_SHT_ADDR_ALT) return EL_ERR_ARG;
    memset(s, 0, sizeof *s);
    s->model = model;
    s->addr = addr7;
    s->io = *io;
    return EL_OK;
}

static uint32_t wait_ms(const el_sht_t *s) { return s->model == EL_SHT3X ? EL_SHT3X_WAIT_MS : EL_SHT4X_WAIT_MS; }

static el_sensor_err_t sht_begin(void *ctx, uint32_t now_ms)
{
    el_sht_t *s = ctx;
    size_t n = 0;
    const uint8_t *cmd = el_sht_meas_cmd(s->model, &n);
    s->busy = false;
    if (s->io.write(s->io.bus, s->addr, cmd, n) != 0) return EL_SENSOR_ERR_NO_ACK;
    s->busy = true;
    s->start_ms = now_ms;
    return EL_SENSOR_OK;
}

static el_sensor_poll_t sht_poll(void *ctx, uint32_t now_ms, el_sensor_sample_t *sample, el_sensor_err_t *err)
{
    el_sht_t *s = ctx;
    el_sensor_err_t dummy;
    if (!err) err = &dummy;
    *err = EL_SENSOR_OK;
    if (!s->busy) { *err = EL_SENSOR_ERR_INTERNAL; return EL_SENSOR_ERROR; }
    uint32_t el = (uint32_t)(now_ms - s->start_ms);     /* wrap-safe */
    if (el < wait_ms(s)) return EL_SENSOR_PENDING;

    uint8_t b[6];
    if (s->io.read(s->io.bus, s->addr, b, sizeof b) != 0) {
        /* Still converting (address NACK) or gone: retry until the timeout. */
        if (el < EL_SHT_READ_TIMEOUT_MS) return EL_SENSOR_PENDING;
        s->busy = false;
        *err = EL_SENSOR_ERR_TIMEOUT;
        return EL_SENSOR_ERROR;
    }
    s->busy = false;
    float t = NAN, rh = NAN;
    el_sensor_err_t e = el_sht_decode(s->model, b, &t, &rh);
    if (e != EL_SENSOR_OK) { *err = e; return EL_SENSOR_ERROR; }
    if (sample) {
        sample->temp_c = t;
        sample->rh_pct = rh;
        sample->quality = EL_SAMPLE_VALID;
    }
    return EL_SENSOR_READY;
}

static void sht_abort(void *ctx)
{
    el_sht_t *s = ctx;
    s->busy = false;      /* a running conversion finishes by itself; its data is never read */
}

static el_err_t sht_info(void *ctx, char *buf, size_t cap)
{
    const el_sht_t *s = ctx;
    if (!buf || !cap) return EL_ERR_ARG;
    int n = snprintf(buf, cap, "%s @0x%02X", el_sht_model_name(s->model), (unsigned)s->addr);
    return (n < 0 || (size_t)n >= cap) ? EL_ERR_SIZE : EL_OK;
}

static void sht_caps(void *ctx, el_sensor_caps_t *out)
{
    const el_sht_t *s = ctx;
    out->flags = 0;
    out->typ_conversion_ms = wait_ms(s);
}

static const el_sensor_ops_t SHT_OPS = {
    .begin_conversion = sht_begin,
    .poll             = sht_poll,
    .abort            = sht_abort,
    .info             = sht_info,
    .caps             = sht_caps,
};

el_sensor_t el_sht_driver(el_sht_t *s)
{
    el_sensor_t d = { &SHT_OPS, s };
    return d;
}
