#include "el_modbus.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "el_stats.h"
#include "el_util.h"

/* ---------- limits for writable settings ---------- */
#define REPEATS_MIN   1
#define REPEATS_MAX   10
#define ADDR_MAX      247
#define BAUD_CODE_MAX 4
#define PARITY_CODE_MAX 2
#define INTERVAL_MIN  10u
#define INTERVAL_MAX  86400u

/* ---------- register tables ---------- */
/* One entry per addressable item. A request must cover whole, adjacent items. */
typedef struct { uint16_t addr; uint8_t nregs; uint8_t id; } field_t;

enum { I_TEMP, I_STATUS, I_COUNTER, I_TIMESTAMP, I_STATE, I_ALARMS, I_PROGRESS, I_FLAGS, I_RH,
       I_REPEATS, I_DEW, I_TEMP_SD, I_SEQ, I_RH_SD, I_UPTIME, I_LOG_COUNT, I_LOG_PCT, I_NEXT_AUTO,
       I_FW_MAJOR, I_FW_MINOR, I_FW_PATCH, I_SERIAL, I_TAG };

enum { H_CMD, H_REPEATS, H_T_LOW, H_T_HIGH, H_RH_LOW, H_RH_HIGH, H_ADDR, H_BAUD, H_PARITY, H_INTERVAL };

static const field_t IN_TAB[] = {
    {EL_MB_IR_TEMP, 2, I_TEMP}, {EL_MB_IR_STATUS, 1, I_STATUS}, {EL_MB_IR_RESULT_COUNTER, 1, I_COUNTER},
    {EL_MB_IR_TIMESTAMP, 2, I_TIMESTAMP}, {EL_MB_IR_MEAS_STATE, 1, I_STATE}, {EL_MB_IR_ALARMS, 1, I_ALARMS},
    {EL_MB_IR_PROGRESS, 1, I_PROGRESS}, {EL_MB_IR_FLAGS, 1, I_FLAGS}, {EL_MB_IR_HUMIDITY, 2, I_RH},
    {EL_MB_IR_REPEATS, 1, I_REPEATS}, {EL_MB_IR_DEW_POINT, 2, I_DEW}, {EL_MB_IR_TEMP_SD, 2, I_TEMP_SD},
    {EL_MB_IR_RESULT_SEQ, 2, I_SEQ}, {EL_MB_IR_RH_SD, 2, I_RH_SD}, {EL_MB_IR_UPTIME, 2, I_UPTIME},
    {EL_MB_IR_LOG_COUNT, 2, I_LOG_COUNT}, {EL_MB_IR_LOG_USED_PCT, 1, I_LOG_PCT},
    {EL_MB_IR_NEXT_AUTO, 2, I_NEXT_AUTO}, {EL_MB_IR_FW_MAJOR, 1, I_FW_MAJOR},
    {EL_MB_IR_FW_MINOR, 1, I_FW_MINOR}, {EL_MB_IR_FW_PATCH, 1, I_FW_PATCH},
    {EL_MB_IR_SERIAL, EL_MB_SERIAL_REGS, I_SERIAL}, {EL_MB_IR_TAG, EL_MB_TAG_REGS, I_TAG},
};

static const field_t HOLD_TAB[] = {
    {EL_MB_HR_COMMAND, 1, H_CMD}, {EL_MB_HR_REPEATS, 1, H_REPEATS}, {EL_MB_HR_T_LOW, 2, H_T_LOW},
    {EL_MB_HR_T_HIGH, 2, H_T_HIGH}, {EL_MB_HR_RH_LOW, 2, H_RH_LOW}, {EL_MB_HR_RH_HIGH, 2, H_RH_HIGH},
    {EL_MB_HR_ADDRESS, 1, H_ADDR}, {EL_MB_HR_BAUD, 1, H_BAUD}, {EL_MB_HR_PARITY, 1, H_PARITY},
    {EL_MB_HR_LOG_INTERVAL, 2, H_INTERVAL},
};

#define TAB_LEN(t) (sizeof(t) / sizeof((t)[0]))

/* Find the items that exactly tile [start, start+qty). Returns 0 if the range starts/ends inside
 * an item or crosses a gap. */
static int walk(const field_t *tab, size_t n, uint16_t start, uint16_t qty, size_t *first, size_t *count)
{
    size_t i = 0;
    while (i < n && tab[i].addr != start) i++;
    *first = i;
    uint32_t got = 0, expect = start;
    size_t c = 0;
    while (got < qty) {
        if (i >= n || tab[i].addr != expect) return 0;
        got += tab[i].nregs;
        expect += tab[i].nregs;
        i++; c++;
    }
    *count = c;
    return got == qty;
}

/* ---------- word helpers ---------- */
uint16_t el_mb_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (uint16_t)(crc ^ data[i]);
        for (int b = 0; b < 8; b++)
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
    }
    return crc;
}

size_t el_mb_frame_seal(uint8_t *buf, size_t len)
{
    uint16_t crc = el_mb_crc16(buf, len);
    buf[len] = (uint8_t)(crc & 0xFF);
    buf[len + 1] = (uint8_t)(crc >> 8);
    return len + 2;
}

void el_mb_put_u32(uint16_t w[2], uint32_t v)
{
    w[0] = (uint16_t)(v >> 16);
    w[1] = (uint16_t)(v & 0xFFFFu);
}

uint32_t el_mb_get_u32(const uint16_t w[2]) { return ((uint32_t)w[0] << 16) | w[1]; }

void el_mb_put_f32(uint16_t w[2], float v)
{
    uint32_t bits;
    memcpy(&bits, &v, sizeof bits);
    el_mb_put_u32(w, bits);
}

float el_mb_get_f32(const uint16_t w[2])
{
    uint32_t bits = el_mb_get_u32(w);
    float v;
    memcpy(&v, &bits, sizeof v);
    return v;
}

void el_mb_put_str(uint16_t *w, size_t nregs, const char *s)
{
    int ended = 0;
    for (size_t r = 0; r < nregs; r++) {
        uint8_t b[2] = {0, 0};
        for (size_t k = 0; k < 2; k++) {
            if (!ended && s[r * 2 + k] != '\0') b[k] = (uint8_t)s[r * 2 + k];
            else ended = 1;
        }
        w[r] = (uint16_t)((b[0] << 8) | b[1]);
    }
}

el_err_t el_mb_get_str(const uint16_t *w, size_t nregs, char *out, size_t cap)
{
    size_t total = nregs * 2, n = 0;
    el_err_t err = EL_OK;
    int ended = 0;
    for (size_t i = 0; i < total; i++) {
        uint8_t b = (uint8_t)((i & 1u) ? (w[i / 2] & 0xFFu) : (w[i / 2] >> 8));
        if (b == 0) { ended = 1; continue; }
        if (ended || b < 0x20 || b > 0x7E) { err = EL_ERR_FORMAT; break; }
        if (cap > 0 && n + 1 < cap) out[n] = (char)b;
        else if (err == EL_OK) err = EL_ERR_SIZE;
        n++;
    }
    if (cap > 0) out[n < cap - 1 ? n : cap - 1] = '\0';
    return err;
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)(v & 0xFF); }

/* ---------- state ---------- */
el_err_t el_mb_init(el_mb_t *mb, uint8_t address)
{
    if (!mb || address < 1 || address > ADDR_MAX) return EL_ERR_ARG;
    memset(mb, 0, sizeof *mb);
    mb->address = address;
    mb->holding.repeats = 3;
    mb->holding.limits.t_low_c = 5.0f;
    mb->holding.limits.t_high_c = 35.0f;
    mb->holding.limits.rh_low_pct = 20.0f;
    mb->holding.limits.rh_high_pct = 80.0f;
    mb->holding.log_interval_s = 300;
    mb->holding.address = address;
    mb->holding.baud_code = 1;
    mb->holding.parity_code = 1;
    return EL_OK;
}

void el_mb_set_gate(el_mb_t *mb, el_mb_gate_fn gate, void *ctx)
{
    mb->gate = gate;
    mb->gate_ctx = ctx;
}

void el_mb_set_inputs(el_mb_t *mb, const el_mb_inputs_t *in)
{
    mb->inputs = *in;
    mb->inputs.serial[sizeof mb->inputs.serial - 1] = '\0';
    mb->inputs.last_tag[sizeof mb->inputs.last_tag - 1] = '\0';
}

void el_mb_set_holding(el_mb_t *mb, const el_mb_holding_t *h)
{
    mb->holding = *h;
}

/* Copy the members named in src->mask from src to dst (both effects). */
static void fx_merge(el_mb_effects_t *dst, const el_mb_effects_t *src)
{
    uint32_t m = src->mask;
    if (m & EL_MB_FX_COMMAND) dst->command = src->command;
    if (m & EL_MB_FX_REPEATS) dst->set.repeats = src->set.repeats;
    if (m & EL_MB_FX_T_LOW) dst->set.limits.t_low_c = src->set.limits.t_low_c;
    if (m & EL_MB_FX_T_HIGH) dst->set.limits.t_high_c = src->set.limits.t_high_c;
    if (m & EL_MB_FX_RH_LOW) dst->set.limits.rh_low_pct = src->set.limits.rh_low_pct;
    if (m & EL_MB_FX_RH_HIGH) dst->set.limits.rh_high_pct = src->set.limits.rh_high_pct;
    if (m & EL_MB_FX_ADDRESS) dst->set.address = src->set.address;
    if (m & EL_MB_FX_BAUD) dst->set.baud_code = src->set.baud_code;
    if (m & EL_MB_FX_PARITY) dst->set.parity_code = src->set.parity_code;
    if (m & EL_MB_FX_INTERVAL) dst->set.log_interval_s = src->set.log_interval_s;
    dst->mask |= m;
}

int el_mb_take_effects(el_mb_t *mb, el_mb_effects_t *out)
{
    int any = mb->pending.mask != 0;
    *out = mb->pending;
    memset(&mb->pending, 0, sizeof mb->pending);
    return any;
}

/* ---------- register reads ---------- */
static size_t in_get(const el_mb_t *mb, uint8_t id, uint16_t *w)
{
    const el_mb_inputs_t *in = &mb->inputs;
    switch (id) {
    case I_TEMP:      el_mb_put_f32(w, in->temp_c); return 2;
    case I_STATUS:    w[0] = in->status; return 1;
    case I_COUNTER:   w[0] = in->result_counter; return 1;
    case I_TIMESTAMP: el_mb_put_u32(w, in->last_timestamp_s); return 2;
    case I_STATE:     w[0] = in->meas_state; return 1;
    case I_ALARMS:    w[0] = in->alarms; return 1;
    case I_PROGRESS:  w[0] = in->progress_pct; return 1;
    case I_FLAGS:     w[0] = in->flags; return 1;
    case I_RH:        el_mb_put_f32(w, in->rh_pct); return 2;
    case I_REPEATS:   w[0] = in->repeats; return 1;
    case I_DEW:       el_mb_put_f32(w, in->dew_point_c); return 2;
    case I_TEMP_SD:   el_mb_put_f32(w, in->temp_sd_c); return 2;
    case I_SEQ:       el_mb_put_u32(w, in->last_seq); return 2;
    case I_RH_SD:     el_mb_put_f32(w, in->rh_sd_pct); return 2;
    case I_UPTIME:    el_mb_put_u32(w, in->uptime_s); return 2;
    case I_LOG_COUNT: el_mb_put_u32(w, in->log_count); return 2;
    case I_LOG_PCT:   w[0] = in->log_used_pct; return 1;
    case I_NEXT_AUTO: el_mb_put_u32(w, in->next_auto_s); return 2;
    case I_FW_MAJOR:  w[0] = in->fw_major; return 1;
    case I_FW_MINOR:  w[0] = in->fw_minor; return 1;
    case I_FW_PATCH:  w[0] = in->fw_patch; return 1;
    case I_SERIAL:    el_mb_put_str(w, EL_MB_SERIAL_REGS, in->serial); return EL_MB_SERIAL_REGS;
    default:          el_mb_put_str(w, EL_MB_TAG_REGS, in->last_tag); return EL_MB_TAG_REGS;
    }
}

static size_t hold_get(const el_mb_t *mb, uint8_t id, uint16_t *w)
{
    const el_mb_holding_t *h = &mb->holding;
    switch (id) {
    case H_CMD:      w[0] = 0; return 1;   /* write-only semantics */
    case H_REPEATS:  w[0] = h->repeats; return 1;
    case H_T_LOW:    el_mb_put_f32(w, h->limits.t_low_c); return 2;
    case H_T_HIGH:   el_mb_put_f32(w, h->limits.t_high_c); return 2;
    case H_RH_LOW:   el_mb_put_f32(w, h->limits.rh_low_pct); return 2;
    case H_RH_HIGH:  el_mb_put_f32(w, h->limits.rh_high_pct); return 2;
    case H_ADDR:     w[0] = h->address; return 1;
    case H_BAUD:     w[0] = h->baud_code; return 1;
    case H_PARITY:   w[0] = h->parity_code; return 1;
    default:         el_mb_put_u32(w, h->log_interval_s); return 2;
    }
}

/* FC03 / FC04. pdu = fc,start(2),qty(2). */
static size_t do_read(const el_mb_t *mb, const uint8_t *pdu, size_t plen, uint8_t *rp, uint8_t *ex)
{
    uint8_t fc = pdu[0];
    if (plen != 5) { *ex = EL_MB_EX_VALUE; return 0; }
    uint16_t start = be16(pdu + 1), qty = be16(pdu + 3);
    if (qty < 1 || qty > EL_MB_MAX_READ_REGS) { *ex = EL_MB_EX_VALUE; return 0; }
    if ((uint32_t)start + qty > 0x10000u) { *ex = EL_MB_EX_ADDRESS; return 0; }
    int input = fc == EL_MB_FC_READ_INPUT;
    const field_t *tab = input ? IN_TAB : HOLD_TAB;
    size_t first, count;
    if (!walk(tab, input ? TAB_LEN(IN_TAB) : TAB_LEN(HOLD_TAB), start, qty, &first, &count)) {
        *ex = EL_MB_EX_ADDRESS;
        return 0;
    }
    rp[0] = fc;
    rp[1] = (uint8_t)(qty * 2);
    size_t o = 2;
    for (size_t i = 0; i < count; i++) {
        uint16_t w[EL_MB_TAG_REGS];
        size_t n = input ? in_get(mb, tab[first + i].id, w) : hold_get(mb, tab[first + i].id, w);
        for (size_t k = 0; k < n; k++, o += 2) put_be16(rp + o, w[k]);
    }
    return o;
}

/* ---------- register writes ---------- */
static int in_range(float v, float lo, float hi) { return isfinite(v) && v >= lo && v <= hi; }

static uint8_t put_float(const uint16_t *w, float lo, float hi, float *dst, uint32_t bit, el_mb_effects_t *fx)
{
    float v = el_mb_get_f32(w);
    if (!in_range(v, lo, hi)) return EL_MB_EX_VALUE;
    *dst = v;
    fx->mask |= bit;
    return 0;
}

/* Validate one item and record it in fx. Returns 0 or an exception code. */
static uint8_t parse_item(uint8_t id, const uint16_t *w, el_mb_effects_t *fx)
{
    el_limits_t *l = &fx->set.limits;
    switch (id) {
    case H_CMD:
        if (w[0] > EL_MB_CMD_CLEAR_ALARM) return EL_MB_EX_VALUE;
        if (w[0] != EL_MB_CMD_NONE) { fx->command = (el_mb_cmd_t)w[0]; fx->mask |= EL_MB_FX_COMMAND; }
        return 0;
    case H_REPEATS:
        if (w[0] < REPEATS_MIN || w[0] > REPEATS_MAX) return EL_MB_EX_VALUE;
        fx->set.repeats = w[0]; fx->mask |= EL_MB_FX_REPEATS;
        return 0;
    case H_T_LOW:   return put_float(w, EL_TEMP_MIN_C, EL_TEMP_MAX_C, &l->t_low_c, EL_MB_FX_T_LOW, fx);
    case H_T_HIGH:  return put_float(w, EL_TEMP_MIN_C, EL_TEMP_MAX_C, &l->t_high_c, EL_MB_FX_T_HIGH, fx);
    case H_RH_LOW:  return put_float(w, EL_RH_MIN_PCT, EL_RH_MAX_PCT, &l->rh_low_pct, EL_MB_FX_RH_LOW, fx);
    case H_RH_HIGH: return put_float(w, EL_RH_MIN_PCT, EL_RH_MAX_PCT, &l->rh_high_pct, EL_MB_FX_RH_HIGH, fx);
    case H_ADDR:
        if (w[0] < 1 || w[0] > ADDR_MAX) return EL_MB_EX_VALUE;
        fx->set.address = w[0]; fx->mask |= EL_MB_FX_ADDRESS;
        return 0;
    case H_BAUD:
        if (w[0] > BAUD_CODE_MAX) return EL_MB_EX_VALUE;
        fx->set.baud_code = w[0]; fx->mask |= EL_MB_FX_BAUD;
        return 0;
    case H_PARITY:
        if (w[0] > PARITY_CODE_MAX) return EL_MB_EX_VALUE;
        fx->set.parity_code = w[0]; fx->mask |= EL_MB_FX_PARITY;
        return 0;
    default: {
        uint32_t v = el_mb_get_u32(w);
        if (v != 0 && (v < INTERVAL_MIN || v > INTERVAL_MAX)) return EL_MB_EX_VALUE;
        fx->set.log_interval_s = v; fx->mask |= EL_MB_FX_INTERVAL;
        return 0;
    }
    }
}

/* Atomic holding write of qty registers (big-endian bytes in data). Returns 0 or exception. */
static uint8_t hold_write(el_mb_t *mb, uint16_t start, uint16_t qty, const uint8_t *data)
{
    size_t first, count;
    if (!walk(HOLD_TAB, TAB_LEN(HOLD_TAB), start, qty, &first, &count)) return EL_MB_EX_ADDRESS;

    el_mb_effects_t fx;
    memset(&fx, 0, sizeof fx);
    size_t r = 0;
    for (size_t i = 0; i < count; i++) {
        const field_t *f = &HOLD_TAB[first + i];
        uint16_t w[EL_MB_TAG_REGS];
        for (size_t k = 0; k < f->nregs; k++) w[k] = be16(data + (r + k) * 2);
        uint8_t ex = parse_item(f->id, w, &fx);
        if (ex) return ex;
        r += f->nregs;
    }

    /* The limits must stay ordered. Judge each pair as it will be after this write. */
    const el_limits_t *o = &mb->holding.limits, *n = &fx.set.limits;
    float tl = (fx.mask & EL_MB_FX_T_LOW) ? n->t_low_c : o->t_low_c;
    float th = (fx.mask & EL_MB_FX_T_HIGH) ? n->t_high_c : o->t_high_c;
    float hl = (fx.mask & EL_MB_FX_RH_LOW) ? n->rh_low_pct : o->rh_low_pct;
    float hh = (fx.mask & EL_MB_FX_RH_HIGH) ? n->rh_high_pct : o->rh_high_pct;
    if ((fx.mask & (EL_MB_FX_T_LOW | EL_MB_FX_T_HIGH)) && tl >= th) return EL_MB_EX_VALUE;
    if ((fx.mask & (EL_MB_FX_RH_LOW | EL_MB_FX_RH_HIGH)) && hl >= hh) return EL_MB_EX_VALUE;

    if (fx.mask == 0) return 0;   /* e.g. command 0: accepted, nothing to do */
    if (mb->gate) {
        el_mb_verdict_t v = mb->gate(mb->gate_ctx, &fx);
        if (v == EL_MB_REJECT_BUSY) return EL_MB_EX_BUSY;
        if (v != EL_MB_ACCEPT) return EL_MB_EX_DEVICE_FAILURE;
    }

    /* Commit: read-back shows the new value; the firmware gets the effect. */
    el_mb_effects_t shown = fx;
    shown.mask &= ~(uint32_t)EL_MB_FX_COMMAND;      /* command is never stored */
    el_mb_effects_t hold_view;
    memset(&hold_view, 0, sizeof hold_view);
    hold_view.set = mb->holding;
    fx_merge(&hold_view, &shown);
    mb->holding = hold_view.set;
    fx_merge(&mb->pending, &fx);
    return 0;
}

/* FC06. pdu = fc,reg(2),value(2). Response echoes the request. */
static size_t do_write_single(el_mb_t *mb, const uint8_t *pdu, size_t plen, uint8_t *rp, uint8_t *ex)
{
    if (plen != 5) { *ex = EL_MB_EX_VALUE; return 0; }
    *ex = hold_write(mb, be16(pdu + 1), 1, pdu + 3);
    if (*ex) return 0;
    memcpy(rp, pdu, 5);
    return 5;
}

/* FC16. pdu = fc,start(2),qty(2),bytecount,data. */
static size_t do_write_multi(el_mb_t *mb, const uint8_t *pdu, size_t plen, uint8_t *rp, uint8_t *ex)
{
    if (plen < 6) { *ex = EL_MB_EX_VALUE; return 0; }
    uint16_t start = be16(pdu + 1), qty = be16(pdu + 3);
    if (qty < 1 || qty > EL_MB_MAX_WRITE_REGS || pdu[5] != qty * 2 || plen != 6u + pdu[5]) {
        *ex = EL_MB_EX_VALUE;
        return 0;
    }
    if ((uint32_t)start + qty > 0x10000u) { *ex = EL_MB_EX_ADDRESS; return 0; }
    *ex = hold_write(mb, start, qty, pdu + 6);
    if (*ex) return 0;
    memcpy(rp, pdu, 5);
    return 5;
}

/* ---------- FC08 and FC43/14 ---------- */
/* FC08 sub-function 0 (return query data): echo. pdu = fc,sub(2),data(2). */
static size_t do_diagnostic(const uint8_t *pdu, size_t plen, uint8_t *rp, uint8_t *ex)
{
    if (plen != 5) { *ex = EL_MB_EX_VALUE; return 0; }
    if (be16(pdu + 1) != 0) { *ex = EL_MB_EX_FUNCTION; return 0; }   /* sub-function unsupported */
    memcpy(rp, pdu, 5);
    return 5;
}

/* Read device identification, basic category: objects 0 VendorName, 1 ProductCode, 2 revision.
 * Codes 1 (stream) and 4 (one object) supported; 2 and 3 (regular/extended) -> exception 03. */
static size_t do_device_id(const el_mb_t *mb, const uint8_t *pdu, size_t plen, uint8_t *rp, uint8_t *ex)
{
    if (plen < 2 || pdu[1] != 0x0E) { *ex = EL_MB_EX_FUNCTION; return 0; }   /* other MEI types */
    if (plen != 4) { *ex = EL_MB_EX_VALUE; return 0; }
    uint8_t code = pdu[2], obj = pdu[3];
    if (code < 1 || code > 4 || code == 2 || code == 3) { *ex = EL_MB_EX_VALUE; return 0; }

    char rev[24];
    snprintf(rev, sizeof rev, "%u.%u.%u", (unsigned)mb->inputs.fw_major, (unsigned)mb->inputs.fw_minor,
             (unsigned)mb->inputs.fw_patch);
    const char *vals[3] = {EL_MB_VENDOR, EL_MB_PRODUCT_CODE, rev};

    uint8_t from = obj, to = 2;
    if (code == 4) {
        if (obj > 2) { *ex = EL_MB_EX_ADDRESS; return 0; }
        to = obj;
    } else if (obj > 2) {
        from = 0;                       /* spec: unknown start object in a stream read restarts at 0 */
    }
    rp[0] = EL_MB_FC_ENCAP; rp[1] = 0x0E; rp[2] = code;
    rp[3] = 0x81;                       /* conformity: basic, stream + individual access */
    rp[4] = 0;                          /* more follows: no */
    rp[5] = 0;                          /* next object id */
    rp[6] = (uint8_t)(to - from + 1);
    size_t o = 7;
    for (uint8_t i = from; i <= to; i++) {
        size_t l = strlen(vals[i]);
        rp[o++] = i;
        rp[o++] = (uint8_t)l;
        memcpy(rp + o, vals[i], l);
        o += l;
    }
    return o;
}

/* ---------- frame engine ---------- */
size_t el_mb_rtu_process(el_mb_t *mb, const uint8_t *req, size_t len, uint8_t *resp, size_t cap)
{
    uint8_t pdu[EL_MB_RTU_MAX_FRAME];
    if (!mb || !req || !resp) return 0;
    mb->stats.frames_rx++;
    if (len < 4 || len > EL_MB_RTU_MAX_FRAME) { mb->stats.bad_length++; return 0; }
    uint16_t crc = (uint16_t)(req[len - 2] | (req[len - 1] << 8));
    if (el_mb_crc16(req, len - 2) != crc) { mb->stats.crc_errors++; return 0; }

    int bcast = req[0] == 0;
    if (bcast) mb->stats.broadcasts++;
    else if (req[0] != mb->address) { mb->stats.other_address++; return 0; }

    const uint8_t *p = req + 1;
    size_t plen = len - 3;
    uint8_t fc = p[0], ex = 0;
    size_t n = 0;
    if (bcast && fc != EL_MB_FC_WRITE_SINGLE && fc != EL_MB_FC_WRITE_MULTI) return 0;  /* writes only */

    switch (fc) {
    case EL_MB_FC_READ_HOLDING:
    case EL_MB_FC_READ_INPUT:  n = do_read(mb, p, plen, pdu, &ex); break;
    case EL_MB_FC_WRITE_SINGLE: n = do_write_single(mb, p, plen, pdu, &ex); break;
    case EL_MB_FC_WRITE_MULTI:  n = do_write_multi(mb, p, plen, pdu, &ex); break;
    case EL_MB_FC_DIAGNOSTIC:   n = do_diagnostic(p, plen, pdu, &ex); break;
    case EL_MB_FC_ENCAP:        n = do_device_id(mb, p, plen, pdu, &ex); break;
    default: ex = EL_MB_EX_FUNCTION; break;
    }
    if (bcast) return 0;                       /* acted on, never answers */

    if (ex) { pdu[0] = (uint8_t)(fc | 0x80); pdu[1] = ex; n = 2; }
    if (cap < n + 3) return 0;                 /* caller's buffer too small: stay silent */
    if (ex) mb->stats.exceptions_tx++; else mb->stats.responses_tx++;
    resp[0] = req[0];
    memcpy(resp + 1, pdu, n);
    return el_mb_frame_seal(resp, n + 1);
}

/* ---------- master side ---------- */
size_t el_mb_master_read(uint8_t *buf, size_t cap, uint8_t addr, uint8_t fc, uint16_t start, uint16_t qty)
{
    if (cap < 8 || qty < 1 || qty > EL_MB_MAX_READ_REGS) return 0;
    buf[0] = addr; buf[1] = fc;
    put_be16(buf + 2, start); put_be16(buf + 4, qty);
    return el_mb_frame_seal(buf, 6);
}

size_t el_mb_master_write_single(uint8_t *buf, size_t cap, uint8_t addr, uint16_t reg, uint16_t value)
{
    if (cap < 8) return 0;
    buf[0] = addr; buf[1] = EL_MB_FC_WRITE_SINGLE;
    put_be16(buf + 2, reg); put_be16(buf + 4, value);
    return el_mb_frame_seal(buf, 6);
}

size_t el_mb_master_write_multi(uint8_t *buf, size_t cap, uint8_t addr, uint16_t start,
                                const uint16_t *values, uint16_t qty)
{
    if (qty < 1 || qty > EL_MB_MAX_WRITE_REGS || cap < 9u + qty * 2u) return 0;
    buf[0] = addr; buf[1] = EL_MB_FC_WRITE_MULTI;
    put_be16(buf + 2, start); put_be16(buf + 4, qty);
    buf[6] = (uint8_t)(qty * 2);
    for (uint16_t i = 0; i < qty; i++) put_be16(buf + 7 + i * 2, values[i]);
    return el_mb_frame_seal(buf, 7u + qty * 2u);
}

el_err_t el_mb_master_parse(const uint8_t *f, size_t len, uint8_t expect_addr, uint8_t expect_fc,
                            el_mb_master_resp_t *out)
{
    if (!f || !out) return EL_ERR_ARG;
    if (len < 5) return EL_ERR_FORMAT;
    if (el_mb_crc16(f, len - 2) != (uint16_t)(f[len - 2] | (f[len - 1] << 8))) return EL_ERR_CRC;
    memset(out, 0, sizeof *out);
    out->address = f[0];
    out->function = f[1] & 0x7Fu;
    if (f[0] != expect_addr || out->function != expect_fc) return EL_ERR_FORMAT;
    if (f[1] & 0x80u) {
        if (len != 5) return EL_ERR_FORMAT;
        out->exception = f[2];
        return EL_OK;
    }
    switch (expect_fc) {
    case EL_MB_FC_READ_HOLDING:
    case EL_MB_FC_READ_INPUT: {
        size_t bc = f[2];
        if (bc % 2 != 0 || len != bc + 5 || bc / 2 > EL_MB_MAX_READ_REGS) return EL_ERR_FORMAT;
        out->quantity = (uint16_t)(bc / 2);
        for (size_t i = 0; i < bc / 2; i++) out->regs[i] = be16(f + 3 + i * 2);
        return EL_OK;
    }
    case EL_MB_FC_WRITE_SINGLE:
        if (len != 8) return EL_ERR_FORMAT;
        out->start = be16(f + 2);
        out->regs[0] = be16(f + 4);
        out->quantity = 1;
        return EL_OK;
    case EL_MB_FC_WRITE_MULTI:
        if (len != 8) return EL_ERR_FORMAT;
        out->start = be16(f + 2);
        out->quantity = be16(f + 4);
        return EL_OK;
    default:
        return EL_ERR_ARG;
    }
}
