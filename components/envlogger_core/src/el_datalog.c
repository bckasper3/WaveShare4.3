#include <string.h>
#include "el_datalog.h"
#include "el_crc.h"
#include "el_util.h"

/* Header block = two independent 64-byte slots holding the same header with a generation
 * counter. Changes write the older slot first: until that write is complete the other slot
 * still holds the previous header, so a cut never leaves the log without a valid header.
 * Slot layout v1: 0 u32 magic "ENLG" (0x474C4E45)  4 u8 version  5 u8 reserved  6 u16 record size
 *   8 u32 generation  12 u32 base_seq  16 i64 created_s  24 i64 cleared_s
 *  32..59 reserved (zero)  60 u32 crc32 over bytes 0..59 */
#define HDR      EL_DATALOG_HEADER_SIZE
#define REC      ((uint32_t)EL_RECORD_SIZE)
#define SLOT     64u
#define HDR_MAGIC 0x474C4E45u

enum { H_MAGIC = 0, H_VERSION = 4, H_RECSIZE = 6, H_GEN = 8, H_BASE = 12, H_CREATED = 16,
       H_CLEARED = 24, H_CRC = 60 };

_Static_assert(2 * SLOT == HDR, "two slots fill the header block");
_Static_assert(HDR % EL_RECORD_SIZE == 0, "records stay aligned");

typedef struct {
    el_err_t err;          /* EL_OK = valid */
    uint32_t gen, base;
    int64_t  created_s, cleared_s;
} slot_t;

static uint32_t rec_offset(uint32_t index) { return HDR + index * REC; }

static void slot_encode(uint8_t *p, uint32_t gen, uint32_t base, int64_t created_s, int64_t cleared_s)
{
    memset(p, 0, SLOT);
    el_put_u32le(p + H_MAGIC, HDR_MAGIC);
    p[H_VERSION] = EL_DATALOG_VERSION;
    el_put_u16le(p + H_RECSIZE, EL_RECORD_SIZE);
    el_put_u32le(p + H_GEN, gen);
    el_put_u32le(p + H_BASE, base);
    el_put_u64le(p + H_CREATED, (uint64_t)created_s);
    el_put_u64le(p + H_CLEARED, (uint64_t)cleared_s);
    el_put_u32le(p + H_CRC, el_crc32(p, H_CRC));
}

static slot_t slot_decode(const uint8_t *p)
{
    slot_t s = { .err = EL_ERR_FORMAT };
    if (el_get_u32le(p + H_MAGIC) != HDR_MAGIC || p[H_VERSION] == 0) return s;
    if (el_get_u32le(p + H_CRC) != el_crc32(p, H_CRC)) { s.err = EL_ERR_CRC; return s; }
    if (p[H_VERSION] > EL_DATALOG_VERSION) { s.err = EL_ERR_VERSION; return s; }
    s.gen  = el_get_u32le(p + H_GEN);
    s.base = el_get_u32le(p + H_BASE);
    if (el_get_u16le(p + H_RECSIZE) != EL_RECORD_SIZE || s.gen == 0 || s.base == 0) return s;
    s.created_s = (int64_t)el_get_u64le(p + H_CREATED);
    s.cleared_s = (int64_t)el_get_u64le(p + H_CLEARED);
    s.err = EL_OK;
    return s;
}

/* Reads both slots; *best = index of the valid slot with the highest generation, or -1. */
static el_err_t read_header(el_datalog_t *log, slot_t s[2], int *best)
{
    uint8_t b[HDR];
    el_err_t err = log->io.read(log->io.ctx, 0, b, HDR);
    if (err != EL_OK) return err;
    *best = -1;
    for (int i = 0; i < 2; i++) {
        s[i] = slot_decode(b + (uint32_t)i * SLOT);
        log->slot_gen[i] = s[i].err == EL_OK ? s[i].gen : 0;
        if (s[i].err == EL_OK && (*best < 0 || s[i].gen > s[*best].gen)) *best = i;
    }
    return EL_OK;
}

/* Rewrites both slots with `base`. Returns an error only if the FIRST write failed, i.e. the
 * change may or may not have reached flash; after that write the change is committed and a
 * failure of the second write only costs redundancy (open() repairs it). */
static el_err_t write_header(el_datalog_t *log, uint32_t base, int64_t cleared_s)
{
    uint8_t b[SLOT];
    int first = log->slot_gen[0] <= log->slot_gen[1] ? 0 : 1;
    uint32_t top = log->slot_gen[0] > log->slot_gen[1] ? log->slot_gen[0] : log->slot_gen[1];
    for (int k = 0; k < 2; k++) {
        int i = k == 0 ? first : 1 - first;
        uint32_t gen = top + 1u + (uint32_t)k;
        slot_encode(b, gen, base, log->created_unix_s, cleared_s);
        el_err_t err = log->io.write_at(log->io.ctx, (uint32_t)i * SLOT, b, SLOT);
        log->slot_gen[i] = err == EL_OK ? gen : 0;
        if (err != EL_OK && k == 0) return err;
    }
    return EL_OK;
}

static el_err_t read_raw(el_datalog_t *log, uint32_t index, uint8_t raw[EL_RECORD_SIZE])
{
    return log->io.read(log->io.ctx, rec_offset(index), raw, REC);
}

static el_err_t decode_at(const el_datalog_t *log, const uint8_t *raw, uint32_t index, el_result_t *out)
{
    el_err_t err = el_record_decode(raw, out);
    if (err != EL_OK) return err;
    /* A valid record in the wrong place (leftover of a cut delete_all, foreign file) is not ours. */
    return out->seq == log->base_seq + index ? EL_OK : EL_ERR_FORMAT;
}

static bool is_fill(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 1; i < n; i++) if (p[i] != p[0]) return false;
    return n == 0 || p[0] == 0x00 || p[0] == 0xFF;
}

/* A record that fails to decode is "torn" (an interrupted append, never acknowledged) when its
 * CRC field is a correct prefix followed by fill: bytes 0..123 were complete or the CRC never
 * got written. Bit rot in a finished record almost never looks like that (~2^-31), so such a
 * record is kept as damaged instead of being cut, which would let its seq be reused. */
static bool looks_torn(const uint8_t raw[EL_RECORD_SIZE])
{
    uint8_t crc[4];
    el_put_u32le(crc, el_crc32(raw, REC - 4u));
    for (uint32_t j = 0; j < 4u; j++) {
        if (memcmp(raw + REC - 4u, crc, j) == 0 && is_fill(raw + REC - 4u + j, 4u - j)) return true;
    }
    return false;
}

/* (Re)starts the file with a fresh header. Used when the header never got complete. */
static el_err_t create(el_datalog_t *log, uint32_t size, uint32_t base, int64_t now_unix_s)
{
    uint8_t b[HDR];
    el_err_t err = size ? log->io.truncate(log->io.ctx, 0) : EL_OK;
    if (err != EL_OK) return err;
    log->created_unix_s = now_unix_s;
    slot_encode(b, 1, base, now_unix_s, 0);
    slot_encode(b + SLOT, 2, base, now_unix_s, 0);
    err = log->io.append(log->io.ctx, b, HDR);
    if (err != EL_OK) return err;
    log->slot_gen[0] = 1;
    log->slot_gen[1] = 2;
    log->base_seq = base;
    return EL_OK;
}

/* Short file: no record was ever acknowledged (they all start at offset 128). Keep the base
 * from a complete slot 0 if there is one, else this is the first boot: base 1. */
static el_err_t open_short(el_datalog_t *log, uint32_t size, int64_t now, el_datalog_report_t *rep)
{
    uint32_t base = 1;
    if (size >= SLOT) {
        uint8_t b[SLOT];
        el_err_t err = log->io.read(log->io.ctx, 0, b, SLOT);
        if (err != EL_OK) return err;
        slot_t s = slot_decode(b);
        if (s.err == EL_ERR_VERSION) return EL_ERR_VERSION;
        if (s.err == EL_OK) base = s.base;
    }
    rep->created = size == 0;
    rep->header_recreated = size != 0;
    rep->bytes_truncated = size;
    return create(log, size, base, now);
}

/* Drops a partial record and a torn last record. Updates *n (records) and the report. */
static el_err_t repair_tail(el_datalog_t *log, uint32_t size, uint32_t *n, el_result_t *last,
                            bool *last_ok, el_datalog_report_t *rep)
{
    uint8_t raw[EL_RECORD_SIZE];
    uint32_t rem = (size - HDR) % REC;
    el_err_t err;
    if (rem) {
        if ((err = log->io.truncate(log->io.ctx, size - rem)) != EL_OK) return err;
        rep->bytes_truncated += rem;
    }
    *n = (size - HDR) / REC;
    *last_ok = false;
    if (*n == 0) return EL_OK;
    if ((err = read_raw(log, *n - 1, raw)) != EL_OK) return err;
    if (el_record_decode(raw, last) == EL_OK) { *last_ok = true; return EL_OK; }
    if (!looks_torn(raw)) { rep->last_record_damaged = true; return EL_OK; }
    if ((err = log->io.truncate(log->io.ctx, rec_offset(*n - 1))) != EL_OK) return err;
    (*n)--;
    rep->torn_record_removed = true;
    rep->bytes_truncated += REC;
    return EL_OK;
}

/* base_seq implied by the records themselves (seq - index of the last, else the first, readable
 * record). false if neither decodes. */
static el_err_t records_base(el_datalog_t *log, uint32_t n, const el_result_t *last, bool last_ok,
                             bool *found, uint32_t *base)
{
    *found = false;
    if (n == 0) return EL_OK;
    if (last_ok && last->seq > n - 1u) { *found = true; *base = last->seq - (n - 1u); return EL_OK; }
    uint8_t raw[EL_RECORD_SIZE];
    el_result_t r;
    el_err_t err = read_raw(log, 0, raw);
    if (err != EL_OK) return err;
    if (el_record_decode(raw, &r) == EL_OK && r.seq != 0) { *found = true; *base = r.seq; }
    return EL_OK;
}

/* No valid header: is there any record of ours (first or last whole one)? Read-only, so a
 * foreign file is rejected before anything is written to it. */
static el_err_t probe_records(el_datalog_t *log, uint32_t size, bool *found)
{
    uint8_t raw[EL_RECORD_SIZE];
    el_result_t r;
    uint32_t n = (size - HDR) / REC;
    *found = false;
    for (uint32_t k = 0; k < 2u && !*found && n; k++) {
        el_err_t err = read_raw(log, k == 0 ? 0 : n - 1u, raw);
        if (err != EL_OK) return err;
        *found = el_record_decode(raw, &r) == EL_OK;
    }
    return EL_OK;
}

/* File with a complete header block: pick the header, repair the tail, cross-check the base
 * against the records, finish a cut delete_all, rewrite a damaged/stale header. Sets *n. */
static el_err_t open_existing(el_datalog_t *log, uint32_t size, int64_t now, uint32_t *n,
                              el_datalog_report_t *rep)
{
    slot_t s[2];
    int best;
    el_result_t last;
    bool last_ok, rfound, repair = false;
    uint32_t base, rbase = 0;
    el_err_t err = read_header(log, s, &best);
    if (err != EL_OK) return err;
    if (s[0].err == EL_ERR_VERSION || s[1].err == EL_ERR_VERSION) return EL_ERR_VERSION;
    if (best < 0 && size - HDR >= REC) {
        bool ours;
        if ((err = probe_records(log, size, &ours)) != EL_OK) return err;
        if (!ours) return EL_ERR_FORMAT;   /* nothing of ours in this file: leave it alone */
    }
    if ((err = repair_tail(log, size, n, &last, &last_ok, rep)) != EL_OK) return err;
    if ((err = records_base(log, *n, &last, last_ok, &rfound, &rbase)) != EL_OK) return err;

    if (best < 0) {
        if (*n == 0) {   /* header creation was cut (or both slots rotted with no records) */
            rep->header_recreated = true;
            rep->bytes_truncated = size;
            return create(log, size, 1, now);
        }
        if (!rfound) return EL_ERR_FORMAT;   /* readable record, impossible seq: not ours */
        base = rbase;
        log->created_unix_s = now;
        repair = true;
    } else {
        base = s[best].base;
        log->created_unix_s = s[best].created_s;
        log->cleared_unix_s = s[best].cleared_s;
        if (rfound && rbase < base) {
            /* Records older than the header's base: delete_all committed its header and was cut
             * before the shrink. Finish it. */
            if ((err = log->io.truncate(log->io.ctx, HDR)) != EL_OK) return err;
            rep->bytes_truncated += *n * REC;
            rep->delete_completed = true;
            rep->last_record_damaged = false;
            *n = 0;
        } else if (rfound && rbase > base) {
            base = rbase;   /* newest slot lost: never fall back to an older base */
            repair = true;
        }
        if (s[0].err != EL_OK || s[1].err != EL_OK || s[0].base != s[1].base) repair = true;
    }
    log->base_seq = base;
    if (!repair) return EL_OK;
    if ((err = write_header(log, base, log->cleared_unix_s)) != EL_OK) return err;
    rep->header_repaired = true;
    return EL_OK;
}

el_err_t el_datalog_open(el_datalog_t *log, const el_datalog_io_t *io, uint32_t max_records,
                         int64_t now_unix_s, el_datalog_report_t *report)
{
    el_datalog_report_t dummy;
    el_datalog_report_t *rep = report ? report : &dummy;
    if (!log || !io || !io->size || !io->read || !io->append || !io->write_at || !io->truncate)
        return EL_ERR_ARG;
    if (max_records == 0 || max_records > EL_DATALOG_MAX_RECORDS_LIMIT) return EL_ERR_ARG;
    memset(log, 0, sizeof *log);
    memset(rep, 0, sizeof *rep);
    log->io = *io;
    log->max_records = max_records;

    uint32_t size, n = 0;
    el_err_t err = io->size(io->ctx, &size);
    if (err != EL_OK) return err;
    err = size < HDR ? open_short(log, size, now_unix_s, rep)
                     : open_existing(log, size, now_unix_s, &n, rep);
    if (err != EL_OK) return err;
    log->count = n;
    log->is_open = true;
    rep->records = n;
    rep->next_seq = log->base_seq + n;
    return EL_OK;
}

/* After a failed header write we do not know whether the change reached flash: re-read it. */
static el_err_t resync_header(el_datalog_t *log)
{
    if (!log->header_uncertain) return EL_OK;
    slot_t s[2];
    int best;
    el_err_t err = read_header(log, s, &best);
    if (err != EL_OK) return err;
    if (best < 0) return EL_ERR_FORMAT;
    if (s[best].base != log->base_seq) {    /* the delete_all did commit */
        log->base_seq = s[best].base;
        log->count = 0;
        log->cleared_unix_s = s[best].cleared_s;
    }
    log->header_uncertain = false;
    return EL_OK;
}

/* A failed append may have left bytes past the last record; cut them before appending again. */
static el_err_t ensure_size(el_datalog_t *log)
{
    uint32_t size, want = rec_offset(log->count);
    el_err_t err = log->io.size(log->io.ctx, &size);
    if (err != EL_OK) return err;
    if (size == want) return EL_OK;
    if (size < want) return EL_ERR_FORMAT;
    return log->io.truncate(log->io.ctx, want);
}

el_err_t el_datalog_append(el_datalog_t *log, const el_result_t *r, uint32_t *out_seq)
{
    if (!log || !r || !log->is_open) return EL_ERR_ARG;
    el_err_t err = resync_header(log);
    if (err != EL_OK) return err;
    /* Product policy: a full log refuses new results; the UI asks the user to export and clear.
     * TODO: ring-overwrite instead (drop the oldest record)? */
    if (log->count >= log->max_records) return EL_ERR_SIZE;
    uint32_t seq = log->base_seq + log->count;
    if (seq == UINT32_MAX) return EL_ERR_RANGE;

    uint8_t raw[EL_RECORD_SIZE];
    el_result_t rec = *r;
    rec.seq = seq;
    if ((err = el_record_encode(&rec, raw)) != EL_OK) return err;
    if ((err = ensure_size(log)) != EL_OK) return err;
    if ((err = log->io.append(log->io.ctx, raw, REC)) != EL_OK) return err;
    log->count++;
    if (out_seq) *out_seq = seq;
    return EL_OK;
}

el_err_t el_datalog_get(el_datalog_t *log, uint32_t index, el_result_t *out)
{
    if (!log || !out || !log->is_open || index >= log->count) return EL_ERR_ARG;
    uint8_t raw[EL_RECORD_SIZE];
    el_err_t err = read_raw(log, index, raw);
    return err != EL_OK ? err : decode_at(log, raw, index, out);
}

static el_err_t iterate(el_datalog_t *log, uint32_t start, uint32_t limit,
                        el_datalog_dir_t dir, el_datalog_cb_t cb, void *ctx, el_datalog_iter_t *st)
{
    el_datalog_iter_t dummy;
    if (!st) st = &dummy;
    memset(st, 0, sizeof *st);
    st->first_bad_index = UINT32_MAX;
    if (!log || !cb || !log->is_open) return EL_ERR_ARG;
    if (dir != EL_DATALOG_OLDEST_FIRST && dir != EL_DATALOG_NEWEST_FIRST) return EL_ERR_ARG;
    if (start >= log->count) return EL_OK;

    bool fwd = dir == EL_DATALOG_OLDEST_FIRST;
    uint32_t avail = fwd ? log->count - start : start + 1u;
    uint32_t todo = limit < avail ? limit : avail;
    uint32_t i = start;
    while (todo) {
        uint32_t nb = todo < EL_DATALOG_BATCH_RECORDS ? todo : EL_DATALOG_BATCH_RECORDS;
        uint32_t lo = fwd ? i : i - (nb - 1u);
        el_err_t err = log->io.read(log->io.ctx, rec_offset(lo), log->batch, nb * REC);
        if (err != EL_OK) return err;
        for (uint32_t k = 0; k < nb; k++) {
            uint32_t idx = fwd ? i + k : i - k;
            el_result_t r;
            st->examined++;
            if (decode_at(log, log->batch + (idx - lo) * REC, idx, &r) != EL_OK) {
                if (st->bad++ == 0) st->first_bad_index = idx;
                continue;
            }
            st->matched++;
            if (!cb(idx, &r, ctx)) { st->stopped = true; return EL_OK; }
        }
        todo -= nb;
        i = fwd ? i + nb : i - nb;   /* may wrap below 0 on the last batch; not used then */
    }
    return EL_OK;
}

el_err_t el_datalog_iterate(el_datalog_t *log, uint32_t start, uint32_t limit,
                            el_datalog_dir_t dir, el_datalog_cb_t cb, void *ctx,
                            el_datalog_iter_t *stats)
{
    return iterate(log, start, limit, dir, cb, ctx, stats);
}


el_err_t el_datalog_find_first_after_seq(const el_datalog_t *log, uint32_t seq, uint32_t *out_index)
{
    if (!log || !out_index || !log->is_open) return EL_ERR_ARG;
    if (seq < log->base_seq) { *out_index = 0; return EL_OK; }
    uint64_t idx = (uint64_t)seq - log->base_seq + 1u;
    *out_index = idx > log->count ? log->count : (uint32_t)idx;
    return EL_OK;
}

el_err_t el_datalog_delete_all(el_datalog_t *log, int64_t now_unix_s)
{
    if (!log || !log->is_open) return EL_ERR_ARG;
    el_err_t err = resync_header(log);
    if (err != EL_OK) return err;
    uint32_t base = log->base_seq + log->count;
    /* Commit point: the new header (base = next_seq) is durable. A cut before it leaves the old
     * log; a cut after it leaves stale records that open() recognises (seq < base) and drops. */
    if ((err = write_header(log, base, now_unix_s)) != EL_OK) {
        log->header_uncertain = true;
        return err;
    }
    log->base_seq = base;
    log->count = 0;
    log->cleared_unix_s = now_unix_s;
    /* Shrink failure is harmless: the next append (ensure_size) or open() finishes it. */
    (void)log->io.truncate(log->io.ctx, HDR);
    return EL_OK;
}

static bool count_cb(uint32_t index, const el_result_t *r, void *ctx)
{
    (void)index; (void)r; (void)ctx;
    return true;
}

el_err_t el_datalog_verify_all(el_datalog_t *log, el_datalog_verify_t *out)
{
    if (!out) return EL_ERR_ARG;
    el_datalog_iter_t st;
    el_err_t err = iterate(log, 0, UINT32_MAX, EL_DATALOG_OLDEST_FIRST, count_cb, NULL, &st);
    out->good = st.matched;
    out->bad = st.bad;
    out->first_bad_index = st.first_bad_index;
    return err;
}

static bool usable(const el_datalog_t *log) { return log && log->is_open; }

uint32_t el_datalog_count(const el_datalog_t *log) { return usable(log) ? log->count : 0; }
uint32_t el_datalog_capacity(const el_datalog_t *log) { return usable(log) ? log->max_records : 0; }
uint32_t el_datalog_next_seq(const el_datalog_t *log) { return usable(log) ? log->base_seq + log->count : 0; }
uint32_t el_datalog_first_seq(const el_datalog_t *log) { return usable(log) ? log->base_seq : 0; }
bool el_datalog_full(const el_datalog_t *log) { return usable(log) && log->count >= log->max_records; }

uint32_t el_datalog_fill_pct(const el_datalog_t *log)
{
    if (!usable(log)) return 0;
    uint64_t pct = (uint64_t)log->count * 100u / log->max_records;
    return pct > 100u ? 100u : (uint32_t)pct;
}

bool el_datalog_near_full(const el_datalog_t *log)
{
    return usable(log) &&
           (uint64_t)log->count * 100u >= (uint64_t)log->max_records * EL_DATALOG_NEAR_FULL_PCT;
}
