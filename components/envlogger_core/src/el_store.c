#include <stdio.h>
#include <string.h>
#include "el_store.h"
#include "el_crc.h"
#include "el_time.h"
#include "el_util.h"

/* ================= Two-slot blobs ================= */

enum { OFF_MAGIC = 0, OFF_VERSION = 2, OFF_TAG = 3, OFF_GEN = 4, OFF_LEN = 8, OFF_RSV = 10 };

el_err_t el_store_slot_encode(uint8_t tag, uint32_t gen, const void *payload, size_t len,
                              uint8_t *out, size_t cap, size_t *out_len)
{
    if (!out || gen == 0 || len > EL_STORE_SLOT_PAYLOAD_MAX || (!payload && len > 0))
        return EL_ERR_ARG;
    size_t total = EL_STORE_SLOT_OVERHEAD + len;
    if (cap < total) return EL_ERR_SIZE;
    el_put_u16le(out + OFF_MAGIC, EL_STORE_SLOT_MAGIC);
    out[OFF_VERSION] = EL_STORE_SLOT_VERSION;
    out[OFF_TAG] = tag;
    el_put_u32le(out + OFF_GEN, gen);
    el_put_u16le(out + OFF_LEN, (uint16_t)len);
    el_put_u16le(out + OFF_RSV, 0);
    if (len) memcpy(out + EL_STORE_SLOT_HDR, payload, len);
    el_put_u32le(out + EL_STORE_SLOT_HDR + len, el_crc32(out, EL_STORE_SLOT_HDR + len));
    if (out_len) *out_len = total;
    return EL_OK;
}

el_err_t el_store_slot_decode(const uint8_t *in, size_t len, uint8_t tag, uint32_t *gen,
                              const uint8_t **payload, size_t *payload_len)
{
    if (!in || len < EL_STORE_SLOT_OVERHEAD) return EL_ERR_FORMAT;
    if (el_get_u16le(in + OFF_MAGIC) != EL_STORE_SLOT_MAGIC) return EL_ERR_FORMAT;
    size_t plen = el_get_u16le(in + OFF_LEN);
    /* Exact length: NVS returns a blob at the length it was written with, so anything else is
     * a torn or foreign value. Checked before the CRC so the CRC read stays in bounds. */
    if (len != EL_STORE_SLOT_OVERHEAD + plen) return EL_ERR_FORMAT;
    if (el_get_u32le(in + EL_STORE_SLOT_HDR + plen) != el_crc32(in, EL_STORE_SLOT_HDR + plen))
        return EL_ERR_CRC;
    /* Version and tag are judged only once the bytes are known to be intact. */
    if (in[OFF_VERSION] > EL_STORE_SLOT_VERSION) return EL_ERR_VERSION;
    if (in[OFF_VERSION] == 0 || in[OFF_TAG] != tag) return EL_ERR_FORMAT;
    uint32_t g = el_get_u32le(in + OFF_GEN);
    if (g == 0) return EL_ERR_FORMAT;
    if (gen) *gen = g;
    if (payload) *payload = in + EL_STORE_SLOT_HDR;
    if (payload_len) *payload_len = plen;
    return EL_OK;
}

bool el_store_gen_newer(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

void el_store_pair_init(el_store_pair_t *p)
{
    if (!p) return;
    p->gen[0] = p->gen[1] = 0;
    p->current = -1;
}

el_err_t el_store_pair_select(el_store_pair_t *p, uint8_t tag,
                              const uint8_t *a, size_t alen, const uint8_t *b, size_t blen,
                              el_store_accept_fn accept, void *ctx,
                              const uint8_t **payload, size_t *payload_len)
{
    if (!p) return EL_ERR_ARG;
    const uint8_t *img[2] = { a, b };
    size_t ilen[2] = { a ? alen : 0, b ? blen : 0 };
    const uint8_t *pl[2] = { NULL, NULL };
    size_t pn[2] = { 0, 0 };
    bool ok[2] = { false, false };

    el_store_pair_init(p);
    for (int i = 0; i < 2; i++) {
        uint32_t g = 0;
        if (el_store_slot_decode(img[i], ilen[i], tag, &g, &pl[i], &pn[i]) != EL_OK) continue;
        /* The generation of an intact but rejected slot is still remembered: the next write
         * must be numbered above it, or a later firmware that accepts it would prefer it. */
        p->gen[i] = g;
        ok[i] = !accept || accept(pl[i], pn[i], ctx);
    }
    int pick = -1;
    if (ok[0] && ok[1]) pick = el_store_gen_newer(p->gen[1], p->gen[0]) ? 1 : 0;  /* tie: slot 0 */
    else if (ok[0]) pick = 0;
    else if (ok[1]) pick = 1;

    if (pick < 0) return (ilen[0] == 0 && ilen[1] == 0) ? EL_ERR_FORMAT : EL_ERR_CRC;
    p->current = (int8_t)pick;
    if (payload) *payload = pl[pick];
    if (payload_len) *payload_len = pn[pick];
    return EL_OK;
}

void el_store_pair_next(const el_store_pair_t *p, uint8_t *slot, uint32_t *gen)
{
    uint8_t s = 0;
    uint32_t hi = 0;
    if (p) {
        if (p->current == 0 || p->current == 1) {
            s = (uint8_t)(1 - p->current);
        } else if (p->gen[0] == 0) {
            s = 0;
        } else if (p->gen[1] == 0) {
            s = 1;
        } else {
            s = el_store_gen_newer(p->gen[0], p->gen[1]) ? 1 : 0;   /* keep the newer copy */
        }
        hi = p->gen[0];
        if (p->gen[1] && (hi == 0 || el_store_gen_newer(p->gen[1], hi))) hi = p->gen[1];
    }
    uint32_t g = hi + 1u;
    if (g == 0) g = 1;        /* 0 means "empty" on flash */
    if (slot) *slot = s;
    if (gen) *gen = g;
}

void el_store_pair_written(el_store_pair_t *p, uint8_t slot, uint32_t gen, bool ok)
{
    if (!p || slot > 1) return;
    if (ok) {
        p->gen[slot] = gen;
        p->current = (int8_t)slot;
    } else {
        /* The write may or may not have landed. Leave current alone so the retry goes to the
         * same slot again; numbering stays above whatever the other slot holds. */
        p->gen[slot] = 0;
        if (p->current == (int8_t)slot) p->current = -1;
    }
}

/* ================= File names ================= */

el_err_t el_store_safe_serial(char *dst, size_t cap, const char *serial)
{
    if (!dst || cap == 0) return EL_ERR_ARG;
    const char *s = (serial && serial[0]) ? serial : "UNSET";
    size_t n = 0;
    for (; s[n] && n < 15; n++) {
        if (n + 1 >= cap) { dst[n] = '\0'; return EL_ERR_SIZE; }
        char c = s[n];
        bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                    || c == '-';
        dst[n] = keep ? c : '_';
    }
    dst[n] = '\0';
    return EL_OK;
}

static el_err_t finish(int w, size_t cap)
{
    return (w < 0 || (size_t)w >= cap) ? EL_ERR_SIZE : EL_OK;
}

el_err_t el_store_export_name(char *dst, size_t cap, const char *serial, int64_t unix_s, unsigned n)
{
    if (!dst || cap == 0) return EL_ERR_ARG;
    char ser[16], ts[16];
    el_store_safe_serial(ser, sizeof ser, serial);
    el_time_format_compact(unix_s < 0 ? 0 : unix_s, ts, sizeof ts);
    int w = n ? snprintf(dst, cap, "ENVLOG_%s_%s_%u.csv", ser, ts, n)
              : snprintf(dst, cap, "ENVLOG_%s_%s.csv", ser, ts);
    return finish(w, cap);
}

el_err_t el_store_mirror_name(char *dst, size_t cap, const char *serial, int64_t unix_s)
{
    if (!dst || cap == 0) return EL_ERR_ARG;
    char ser[16], ts[16];
    el_store_safe_serial(ser, sizeof ser, serial);
    el_time_format_compact(unix_s < 0 ? 0 : unix_s, ts, sizeof ts);
    ts[8] = '\0';                                   /* YYYYMMDD */
    return finish(snprintf(dst, cap, "ENVLOG_%s_%s.csv", ser, ts), cap);
}

/* ================= Export ================= */

el_err_t el_store_export_range(el_store_exp_scope_t scope, uint32_t watermark, uint32_t first_seq,
                               uint32_t next_seq, uint32_t *from_seq, uint32_t *end_seq)
{
    if (!from_seq || !end_seq || (unsigned)scope > EL_STORE_EXP_SINCE_LAST || first_seq > next_seq)
        return EL_ERR_ARG;
    uint32_t from = first_seq;
    if (scope == EL_STORE_EXP_SINCE_LAST && watermark < next_seq && watermark >= first_seq)
        from = watermark + 1u;
    *from_seq = from;
    *end_seq = next_seq;
    return EL_OK;
}

uint32_t el_store_export_watermark(el_store_exp_scope_t scope, uint32_t old_wm, uint32_t end_seq)
{
    (void)scope;
    if (end_seq == 0) return old_wm;
    uint32_t last = end_seq - 1u;
    return last > old_wm ? last : old_wm;
}

uint8_t el_store_pct(uint32_t done, uint32_t total)
{
    if (total == 0 || done >= total) return 100;
    return (uint8_t)(((uint64_t)done * 100u) / total);
}

/* ================= SD mirror ================= */

void el_store_mirror_init(el_store_mirror_t *m, uint32_t saved_seq, uint32_t now_ms)
{
    if (!m) return;
    m->done_seq = saved_seq;
    m->saved_seq = saved_seq;
    m->saved_ms = now_ms;
}

bool el_store_mirror_next(el_store_mirror_t *m, uint32_t first_seq, uint32_t next_seq,
                          uint32_t max_n, uint32_t *from, uint32_t *n)
{
    if (from) *from = 0;
    if (n) *n = 0;
    if (!m || !from || !n || max_n == 0 || first_seq > next_seq) return false;
    /* seq never goes back, so a done_seq at or above next_seq means the log was replaced
     * (e.g. reformatted): start over from what exists. */
    if (m->done_seq >= next_seq) m->done_seq = first_seq ? first_seq - 1u : 0;
    uint32_t f = m->done_seq + 1u;
    if (f < first_seq) f = first_seq;
    if (f >= next_seq) return false;
    uint32_t left = next_seq - f;
    *from = f;
    *n = left < max_n ? left : max_n;
    return true;
}

void el_store_mirror_done(el_store_mirror_t *m, uint32_t seq)
{
    if (m && seq > m->done_seq) m->done_seq = seq;
}

bool el_store_mirror_behind(const el_store_mirror_t *m, uint32_t first_seq, uint32_t next_seq)
{
    if (!m || first_seq >= next_seq) return false;
    if (m->done_seq >= next_seq) return true;               /* replaced log: will restart */
    uint32_t f = m->done_seq + 1u;
    if (f < first_seq) f = first_seq;
    return f < next_seq;
}

bool el_store_mirror_save_due(const el_store_mirror_t *m, uint32_t now_ms, uint32_t interval_ms)
{
    if (!m || m->done_seq == m->saved_seq) return false;
    return (uint32_t)(now_ms - m->saved_ms) >= interval_ms;   /* wrap-safe */
}

void el_store_mirror_saved(el_store_mirror_t *m, uint32_t now_ms)
{
    if (!m) return;
    m->saved_seq = m->done_seq;
    m->saved_ms = now_ms;
}

/* ================= Event log ================= */

size_t el_store_evline_format(char *dst, size_t cap, int64_t unix_s, uint32_t uptime_ms,
                              const char *msg)
{
    if (!dst || cap < EL_STORE_EVLINE_MIN_CAP) return 0;
    char ts[24];
    if (unix_s >= EL_STORE_TIME_VALID_S) el_time_format_iso8601(unix_s, ts, sizeof ts);
    else el_strlcpy(ts, "----------T--:--:--Z", sizeof ts);
    int w = snprintf(dst, cap, "%s up=%lu.%03lu ", ts, (unsigned long)(uptime_ms / 1000u),
                     (unsigned long)(uptime_ms % 1000u));
    if (w < 0) { dst[0] = '\0'; return 0; }
    size_t n = (size_t)w;
    if (n + 2 > cap) n = cap - 2;                 /* cannot happen with MIN_CAP; defensive */
    for (const char *s = msg ? msg : ""; *s && n < cap - 2; s++) {     /* room for "\n\0" */
        unsigned char c = (unsigned char)*s;
        dst[n++] = (c < 0x20 || c == 0x7F) ? ' ' : (char)c;
    }
    dst[n++] = '\n';
    dst[n] = '\0';
    return n;
}

bool el_store_evlog_rotate_due(uint32_t cur_size, uint32_t add, uint32_t limit)
{
    if (cur_size == 0) return false;
    return (uint64_t)cur_size + add > limit;
}
