#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "el_datalog.h"
#include "el_crc.h"
#include "el_util.h"

/* ---- RAM-backed storage with fault injection -------------------------------------------------
 * Power cut model: when armed, writes persist at most `budget` more bytes (a truncate costs 1).
 * The write that runs out persists only its first bytes; the rest of its range is left short
 * (CUT_SHORT: file not grown / old bytes kept) or filled with 0xFF / 0x00, and the device is
 * "dead": every further call fails until reboot(). */
#define IO_ERR EL_ERR_RANGE    /* stand-in for an I/O error code (el_err.h has none yet) */
#define BIG_RECORDS 50000u

typedef enum { CUT_SHORT, CUT_FILL_FF, CUT_FILL_00, CUT_MODES } cut_mode_t;

typedef struct {
    uint8_t *mem;
    uint32_t cap, size;
    bool armed, dead;
    uint32_t budget;
    cut_mode_t mode;
    uint32_t reads, read_bytes, size_calls;
    uint32_t fail_read_from;       /* reads with number >= this fail (0 = never) */
    bool fail_size, fail_append, fail_write, fail_trunc;
    bool append_lies, write_lies;  /* write everything durably, then report an error */
} ram_t;

static uint8_t g_mem[EL_DATALOG_HEADER_SIZE + (BIG_RECORDS + 8u) * EL_RECORD_SIZE];
static ram_t g_ram;
static el_datalog_t g_log;
static el_datalog_report_t g_rep;

static uint32_t take(ram_t *m, uint32_t want)
{
    if (!m->armed) return want;
    uint32_t got = want <= m->budget ? want : m->budget;
    m->budget -= got;
    if (got < want) m->dead = true;
    return got;
}

static uint8_t fill_byte(const ram_t *m) { return m->mode == CUT_FILL_FF ? 0xFF : 0x00; }

static el_err_t ram_size(void *ctx, uint32_t *out)
{
    ram_t *m = ctx;
    m->size_calls++;
    if (m->dead || m->fail_size) return IO_ERR;
    *out = m->size;
    return EL_OK;
}

static el_err_t ram_read(void *ctx, uint32_t off, void *buf, uint32_t len)
{
    ram_t *m = ctx;
    m->reads++;
    m->read_bytes += len;
    if (m->dead || (m->fail_read_from && m->reads >= m->fail_read_from)) return IO_ERR;
    TEST_ASSERT_TRUE_MESSAGE(off + len <= m->size, "engine read past end of file");
    memcpy(buf, m->mem + off, len);
    return EL_OK;
}

static el_err_t ram_append(void *ctx, const void *buf, uint32_t len)
{
    ram_t *m = ctx;
    if (m->dead || m->fail_append) return IO_ERR;
    TEST_ASSERT_TRUE(m->size + len <= m->cap);
    uint32_t got = take(m, len);
    memcpy(m->mem + m->size, buf, got);
    if (got < len) {
        if (m->mode != CUT_SHORT) memset(m->mem + m->size + got, fill_byte(m), len - got);
        m->size += m->mode == CUT_SHORT ? got : len;
        return IO_ERR;
    }
    m->size += len;
    return m->append_lies ? IO_ERR : EL_OK;
}

static el_err_t ram_write_at(void *ctx, uint32_t off, const void *buf, uint32_t len)
{
    ram_t *m = ctx;
    if (m->dead || m->fail_write) return IO_ERR;
    TEST_ASSERT_TRUE_MESSAGE(off + len <= m->size, "write_at must not extend the file");
    uint32_t got = take(m, len);
    memcpy(m->mem + off, buf, got);
    if (got < len) {
        if (m->mode != CUT_SHORT) memset(m->mem + off + got, fill_byte(m), len - got);
        return IO_ERR;
    }
    return m->write_lies ? IO_ERR : EL_OK;
}

static el_err_t ram_truncate(void *ctx, uint32_t len)
{
    ram_t *m = ctx;
    if (m->dead || m->fail_trunc) return IO_ERR;
    TEST_ASSERT_TRUE_MESSAGE(len <= m->size, "truncate must only shrink");
    if (take(m, 1) == 0) return IO_ERR;
    m->size = len;
    return EL_OK;
}

static const el_datalog_io_t g_io = { &g_ram, ram_size, ram_read, ram_append, ram_write_at, ram_truncate };

static void ram_reset(void)
{
    memset(&g_ram, 0, sizeof g_ram);
    g_ram.mem = g_mem;
    g_ram.cap = sizeof g_mem;
}

static void arm(uint32_t budget, cut_mode_t mode)
{
    g_ram.armed = true;
    g_ram.budget = budget;
    g_ram.mode = mode;
}

static void clear_faults(void)
{
    g_ram.armed = g_ram.dead = false;
    g_ram.fail_size = g_ram.fail_append = g_ram.fail_write = g_ram.fail_trunc = false;
    g_ram.append_lies = g_ram.write_lies = false;
    g_ram.fail_read_from = 0;
}

static el_err_t reopen(uint32_t max_records)
{
    clear_faults();
    g_ram.reads = g_ram.read_bytes = g_ram.size_calls = 0;
    return el_datalog_open(&g_log, &g_io, max_records, 1793361600, &g_rep);
}

static bool report_clean(const el_datalog_report_t *r)
{
    return r->bytes_truncated == 0 && !r->created && !r->header_recreated && !r->header_repaired &&
           !r->torn_record_removed && !r->last_record_damaged && !r->delete_completed;
}

/* ---- Results: content is a function of seq, so any record can be checked against its seq ---- */
static el_result_t result_for(uint32_t seq)
{
    el_result_t r = {0};
    r.timestamp_s = 1793361600 + (int64_t)seq * 60;
    snprintf(r.tag, sizeof r.tag, "Room-%u", (unsigned)(seq % 3u));
    el_strlcpy(r.operator_id, "JD", sizeof r.operator_id);
    r.temp_c = 10.0f + (float)seq * 0.5f;
    r.temp_sd_c = 0.04f;
    r.rh_pct = 40.0f + (float)(seq % 20u);
    r.rh_sd_pct = 0.2f;
    r.repeats = 3;
    r.status = EL_RES_NORMAL;
    r.limits.t_low_c = 5.0f; r.limits.t_high_c = 35.0f;
    r.limits.rh_low_pct = 20.0f; r.limits.rh_high_pct = 80.0f;
    return r;
}

static uint32_t append_ok(void)
{
    uint32_t seq = 0, want = el_datalog_next_seq(&g_log);
    el_result_t r = result_for(want);
    r.seq = 0xDEADBEEF;   /* ignored: the log assigns seq */
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_append(&g_log, &r, &seq));
    TEST_ASSERT_EQUAL_UINT32(want, seq);
    return seq;
}

static void assert_record(uint32_t index, uint32_t seq)
{
    el_result_t got, want = result_for(seq);
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_get(&g_log, index, &got));
    TEST_ASSERT_EQUAL_UINT32(seq, got.seq);
    TEST_ASSERT_EQUAL_STRING(want.tag, got.tag);
    TEST_ASSERT_EQUAL_FLOAT(want.temp_c, got.temp_c);
    TEST_ASSERT_EQUAL_FLOAT(want.rh_pct, got.rh_pct);
    TEST_ASSERT_EQUAL_INT64(want.timestamp_s, got.timestamp_s);
}

/* Every record of the open log is intact and numbered first_seq + index. */
static void assert_log(uint32_t first_seq, uint32_t count)
{
    TEST_ASSERT_EQUAL_UINT32(count, el_datalog_count(&g_log));
    TEST_ASSERT_EQUAL_UINT32(first_seq, el_datalog_first_seq(&g_log));
    TEST_ASSERT_EQUAL_UINT32(first_seq + count, el_datalog_next_seq(&g_log));
    for (uint32_t i = 0; i < count; i++) assert_record(i, first_seq + i);
}

static void fresh_log(uint32_t max_records, uint32_t records)
{
    ram_reset();
    TEST_ASSERT_EQUAL(EL_OK, reopen(max_records));
    for (uint32_t i = 0; i < records; i++) append_ok();
}

static void flip_bit(uint32_t offset, unsigned bit) { g_mem[offset] ^= (uint8_t)(1u << bit); }
static uint32_t rec_off(uint32_t index) { return EL_DATALOG_HEADER_SIZE + index * EL_RECORD_SIZE; }

/* ---- Basic behaviour ------------------------------------------------------------------------ */
static void test_new_log_is_created_and_persists(void)
{
    ram_reset();
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    TEST_ASSERT_TRUE(g_rep.created);
    TEST_ASSERT_EQUAL_UINT32(EL_DATALOG_HEADER_SIZE, g_ram.size);
    TEST_ASSERT_EQUAL_UINT32(1, g_rep.next_seq);
    assert_log(1, 0);
    for (int i = 0; i < 3; i++) append_ok();
    TEST_ASSERT_EQUAL_UINT32(EL_DATALOG_HEADER_SIZE + 3u * EL_RECORD_SIZE, g_ram.size);
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    TEST_ASSERT_TRUE(report_clean(&g_rep));
    TEST_ASSERT_EQUAL_UINT32(3, g_rep.records);
    assert_log(1, 3);
}

static void test_records_sit_at_fixed_offsets(void)
{
    /* Record i at 128 * (i + 1): what docs/DATALOG.md promises PC tools. */
    fresh_log(100, 4);
    el_result_t r;
    TEST_ASSERT_EQUAL(EL_OK, el_record_decode(g_mem + 128u * 3u, &r));
    TEST_ASSERT_EQUAL_UINT32(3, r.seq);
    TEST_ASSERT_EQUAL_HEX32(0x474C4E45, el_get_u32le(g_mem));          /* "ENLG" slot 0 */
    TEST_ASSERT_EQUAL_HEX32(0x474C4E45, el_get_u32le(g_mem + 64));     /* slot 1 */
}

static void test_bad_arguments(void)
{
    el_datalog_t log;
    el_result_t r = result_for(1);
    el_datalog_io_t io = g_io;
    uint32_t idx;
    ram_reset();
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_datalog_open(NULL, &g_io, 10, 0, NULL));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_datalog_open(&log, NULL, 10, 0, NULL));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_datalog_open(&log, &g_io, 0, 0, NULL));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_datalog_open(&log, &g_io, EL_DATALOG_MAX_RECORDS_LIMIT + 1u, 0, NULL));
    io.write_at = NULL;
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_datalog_open(&log, &io, 10, 0, NULL));
    TEST_ASSERT_EQUAL_UINT32(0, g_ram.size);   /* nothing written on bad args */

    memset(&log, 0, sizeof log);               /* never opened */
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_datalog_append(&log, &r, NULL));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_datalog_get(&log, 0, &r));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_datalog_delete_all(&log, 0));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_datalog_find_first_after_seq(&log, 0, &idx));
    TEST_ASSERT_EQUAL_UINT32(0, el_datalog_count(&log));
    TEST_ASSERT_EQUAL_UINT32(0, el_datalog_count(NULL));
    TEST_ASSERT_FALSE(el_datalog_near_full(NULL));

    fresh_log(10, 2);
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_datalog_append(&g_log, NULL, NULL));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_datalog_get(&g_log, 2, &r));       /* index == count */
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_datalog_get(&g_log, 0, NULL));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_datalog_iterate(&g_log, 0, 10, EL_DATALOG_OLDEST_FIRST, NULL, NULL, NULL));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_datalog_verify_all(&g_log, NULL));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_datalog_find_first_after_seq(&g_log, 0, NULL));
}

static void test_bad_status_is_refused_without_writing(void)
{
    fresh_log(10, 2);
    uint32_t size = g_ram.size;
    el_result_t r = result_for(3);
    r.status = EL_RES_STATUS_COUNT;
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_datalog_append(&g_log, &r, NULL));
    TEST_ASSERT_EQUAL_UINT32(size, g_ram.size);
    assert_log(1, 2);
}

/* ---- (1) Power cut during append at every byte --------------------------------------------- */
static void test_power_cut_during_append_at_every_byte(void)
{
    char msg[64];
    for (int mode = 0; mode < CUT_MODES; mode++) {
        for (uint32_t k = 0; k <= EL_RECORD_SIZE; k++) {
            snprintf(msg, sizeof msg, "mode %d, cut after %u bytes", mode, (unsigned)k);
            fresh_log(100, 5);                          /* seq 1..5 acknowledged */
            el_result_t r = result_for(6);
            arm(k, (cut_mode_t)mode);
            el_err_t err = el_datalog_append(&g_log, &r, NULL);
            TEST_ASSERT_EQUAL_MESSAGE(k == EL_RECORD_SIZE ? EL_OK : IO_ERR, err, msg);

            TEST_ASSERT_EQUAL_MESSAGE(EL_OK, reopen(100), msg);     /* reboot */
            uint32_t n = el_datalog_count(&g_log);
            /* Acknowledged records always survive; the cut record is either complete or gone. */
            TEST_ASSERT_TRUE_MESSAGE(n == 5 || n == 6, msg);
            if (err == EL_OK) TEST_ASSERT_EQUAL_UINT32_MESSAGE(6, n, msg);
            TEST_ASSERT_FALSE_MESSAGE(g_rep.last_record_damaged, msg);
            TEST_ASSERT_FALSE_MESSAGE(g_rep.header_repaired, msg);
            if (n == 5 && g_ram.size > rec_off(5)) TEST_FAIL_MESSAGE(msg);
            assert_log(1, n);
            uint32_t seq = append_ok();                 /* a fresh, never used seq */
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(n + 1u, seq, msg);
            TEST_ASSERT_EQUAL(EL_OK, reopen(100));
            TEST_ASSERT_TRUE_MESSAGE(report_clean(&g_rep), msg);
            assert_log(1, n + 1u);
        }
    }
}

static void test_cut_append_reports_what_was_removed(void)
{
    fresh_log(100, 5);
    el_result_t r = result_for(6);
    arm(70, CUT_SHORT);
    TEST_ASSERT_EQUAL(IO_ERR, el_datalog_append(&g_log, &r, NULL));
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    TEST_ASSERT_EQUAL_UINT32(70, g_rep.bytes_truncated);
    TEST_ASSERT_FALSE(g_rep.torn_record_removed);

    fresh_log(100, 5);
    arm(70, CUT_FILL_FF);           /* file grew by a whole record, mostly erased flash */
    TEST_ASSERT_EQUAL(IO_ERR, el_datalog_append(&g_log, &r, NULL));
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    TEST_ASSERT_TRUE(g_rep.torn_record_removed);
    TEST_ASSERT_EQUAL_UINT32(EL_RECORD_SIZE, g_rep.bytes_truncated);
    TEST_ASSERT_EQUAL_UINT32(5, g_rep.records);
    TEST_ASSERT_EQUAL_UINT32(6, g_rep.next_seq);
}

/* ---- (2) Power cut during delete_all and header creation ------------------------------------ */
static void cut_delete_sweep(uint32_t prior_deletes)
{
    char msg[80];
    for (int mode = 0; mode < CUT_MODES; mode++) {
        for (uint32_t k = 0; k <= 2u * 64u + 1u; k++) {
            snprintf(msg, sizeof msg, "prior %u, mode %d, cut after %u", (unsigned)prior_deletes, mode, (unsigned)k);
            fresh_log(100, 3);
            for (uint32_t d = 0; d < prior_deletes; d++) {
                TEST_ASSERT_EQUAL(EL_OK, el_datalog_delete_all(&g_log, 1793400000));
                append_ok(); append_ok(); append_ok();
            }
            uint32_t first = el_datalog_first_seq(&g_log), next = el_datalog_next_seq(&g_log);
            arm(k, (cut_mode_t)mode);
            el_err_t err = el_datalog_delete_all(&g_log, 1793500000);

            TEST_ASSERT_EQUAL_MESSAGE(EL_OK, reopen(100), msg);
            uint32_t n = el_datalog_count(&g_log);
            if (n == 3) assert_log(first, 3);           /* old log, untouched */
            else {
                TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, n, msg);
                assert_log(next, 0);                    /* empty, seq continues */
                TEST_ASSERT_EQUAL_UINT32_MESSAGE(EL_DATALOG_HEADER_SIZE, g_ram.size, msg);
            }
            if (err == EL_OK) TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, n, msg);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(next, el_datalog_next_seq(&g_log), msg);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(next, append_ok(), msg);
            TEST_ASSERT_EQUAL(EL_OK, reopen(100));
            TEST_ASSERT_TRUE_MESSAGE(report_clean(&g_rep), msg);
            TEST_ASSERT_EQUAL_UINT32(next + 1u, el_datalog_next_seq(&g_log));
        }
    }
}

static void test_power_cut_during_delete_all_at_every_byte(void)
{
    cut_delete_sweep(0);
    cut_delete_sweep(1);
    cut_delete_sweep(2);   /* slots alternate: both orders of the two header writes */
}

static void test_delete_cut_before_shrink_is_finished_on_open(void)
{
    fresh_log(100, 4);
    arm(128, CUT_SHORT);           /* both header slots written, shrink never happens */
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_delete_all(&g_log, 1793500000));
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    TEST_ASSERT_TRUE(g_rep.delete_completed);
    TEST_ASSERT_EQUAL_UINT32(4u * EL_RECORD_SIZE, g_rep.bytes_truncated);
    assert_log(5, 0);
}

static void test_delete_shrink_cut_halfway_is_finished_on_open(void)
{
    /* truncate may stop at any length between old and new: here 2 of 4 records remain. */
    fresh_log(100, 4);
    uint32_t old = g_ram.size;
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_delete_all(&g_log, 1793500000));
    g_ram.size = old - 2u * EL_RECORD_SIZE - 50u;   /* stale bytes are still in g_mem */
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    TEST_ASSERT_TRUE(g_rep.delete_completed);
    assert_log(5, 0);
    TEST_ASSERT_EQUAL_UINT32(5, append_ok());
}

static void test_power_cut_during_header_creation_at_every_byte(void)
{
    char msg[64];
    for (int mode = 0; mode < CUT_MODES; mode++) {
        for (uint32_t k = 0; k <= EL_DATALOG_HEADER_SIZE; k++) {
            snprintf(msg, sizeof msg, "mode %d, cut after %u", mode, (unsigned)k);
            ram_reset();
            arm(k, (cut_mode_t)mode);
            el_err_t err = el_datalog_open(&g_log, &g_io, 100, 1793361600, &g_rep);
            TEST_ASSERT_EQUAL_MESSAGE(k == EL_DATALOG_HEADER_SIZE ? EL_OK : IO_ERR, err, msg);
            TEST_ASSERT_EQUAL_MESSAGE(EL_OK, reopen(100), msg);
            assert_log(1, 0);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, append_ok(), msg);
            TEST_ASSERT_EQUAL(EL_OK, reopen(100));
            TEST_ASSERT_TRUE_MESSAGE(report_clean(&g_rep), msg);
            assert_log(1, 1);
        }
    }
}

static void test_power_cut_during_open_repair(void)
{
    /* Open itself writes (truncate, header rewrite). A cut there must also be recoverable. */
    char msg[64];
    for (uint32_t k = 0; k < CUT_MODES * 130u; k++) {    /* truncate (1) + two slots (64 + 64) */
        uint32_t budget = k % 130u;
        snprintf(msg, sizeof msg, "mode %u, cut after %u", (unsigned)(k / 130u), (unsigned)budget);
        fresh_log(100, 3);
        TEST_ASSERT_EQUAL(EL_OK, el_datalog_delete_all(&g_log, 1793500000));
        append_ok(); append_ok();                      /* seq 4, 5 */
        flip_bit(10, 0);                               /* slot 0 rotted: open rewrites the header */
        g_ram.size += 40;                              /* plus a partial record to cut */
        arm(budget, (cut_mode_t)(k / 130u));
        (void)el_datalog_open(&g_log, &g_io, 100, 0, &g_rep);
        TEST_ASSERT_EQUAL_MESSAGE(EL_OK, reopen(100), msg);
        assert_log(4, 2);
        TEST_ASSERT_EQUAL(EL_OK, reopen(100));
        TEST_ASSERT_TRUE_MESSAGE(report_clean(&g_rep), msg);
    }
}

/* Random appends / deletes with random power cuts; the model checks every reboot. */
static uint32_t rng(uint32_t *s)
{
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return *s;
}

static void test_random_power_cut_torture(void)
{
    uint32_t seed = 0x1234567u, base = 1, count = 0, max_acked = 0;
    char msg[64];
    fresh_log(40, 0);
    for (int it = 0; it < 4000; it++) {
        snprintf(msg, sizeof msg, "iteration %d", it);
        bool del = count >= 40 || rng(&seed) % 16u == 0;
        bool cut = rng(&seed) % 2u == 0;
        if (cut) arm(rng(&seed) % 200u, (cut_mode_t)(rng(&seed) % CUT_MODES));
        el_err_t err;
        if (del) err = el_datalog_delete_all(&g_log, 1793500000 + it);
        else {
            el_result_t r = result_for(base + count);
            uint32_t seq;
            err = el_datalog_append(&g_log, &r, &seq);
            if (err == EL_OK) max_acked = seq;
        }
        if (err == EL_OK) {
            if (del) { base += count; count = 0; } else count++;
            if (!g_ram.dead) continue;                  /* cut hit nothing yet: keep going */
        }
        TEST_ASSERT_EQUAL_MESSAGE(EL_OK, reopen(40), msg);
        uint32_t n = el_datalog_count(&g_log), first = el_datalog_first_seq(&g_log);
        if (err == EL_OK) {
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(count, n, msg);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(base, first, msg);
        } else if (del) {
            TEST_ASSERT_TRUE_MESSAGE((n == count && first == base) || (n == 0 && first == base + count), msg);
        } else {
            TEST_ASSERT_TRUE_MESSAGE(first == base && (n == count || n == count + 1u), msg);
        }
        base = first;
        count = n;
        assert_log(base, count);
        TEST_ASSERT_TRUE_MESSAGE(el_datalog_next_seq(&g_log) > max_acked, msg);
        TEST_ASSERT_EQUAL(EL_OK, reopen(40));
        TEST_ASSERT_TRUE_MESSAGE(report_clean(&g_rep), msg);
    }
}

/* ---- (3) Bit rot ------------------------------------------------------------------------------ */
static bool count_cb(uint32_t index, const el_result_t *r, void *ctx)
{
    (void)index; (void)r;
    (*(uint32_t *)ctx)++;
    return true;
}

static void test_bit_flip_in_middle_record(void)
{
    fresh_log(100, 10);
    flip_bit(rec_off(4) + 45u, 3);                     /* inside the temperature field */
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    TEST_ASSERT_TRUE(report_clean(&g_rep));            /* open does not scan the middle */
    TEST_ASSERT_EQUAL_UINT32(10, el_datalog_count(&g_log));
    el_result_t r;
    TEST_ASSERT_EQUAL(EL_ERR_CRC, el_datalog_get(&g_log, 4, &r));
    assert_record(3, 4);
    assert_record(5, 6);

    uint32_t seen = 0;
    el_datalog_iter_t st;
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_iterate(&g_log, 0, UINT32_MAX, EL_DATALOG_OLDEST_FIRST, count_cb, &seen, &st));
    TEST_ASSERT_EQUAL_UINT32(9, seen);
    TEST_ASSERT_EQUAL_UINT32(1, st.bad);
    TEST_ASSERT_EQUAL_UINT32(4, st.first_bad_index);
    TEST_ASSERT_EQUAL_UINT32(10, st.examined);

    el_datalog_verify_t v;
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_verify_all(&g_log, &v));
    TEST_ASSERT_EQUAL_UINT32(9, v.good);
    TEST_ASSERT_EQUAL_UINT32(1, v.bad);
    TEST_ASSERT_EQUAL_UINT32(4, v.first_bad_index);

    TEST_ASSERT_EQUAL_UINT32(11, append_ok());         /* log keeps working */
    assert_record(10, 11);
}

static void test_bit_flip_in_last_record_keeps_it_and_its_seq(void)
{
    /* Bit rot in a finished record must not be mistaken for a torn write: cutting it would let
     * its seq (maybe already exported to a PC) be given to a new result. */
    fresh_log(100, 10);
    flip_bit(rec_off(9) + 20u, 1);
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    TEST_ASSERT_TRUE(g_rep.last_record_damaged);
    TEST_ASSERT_FALSE(g_rep.torn_record_removed);
    TEST_ASSERT_EQUAL_UINT32(10, el_datalog_count(&g_log));
    TEST_ASSERT_EQUAL_UINT32(11, append_ok());
    el_result_t r;
    TEST_ASSERT_EQUAL(EL_ERR_CRC, el_datalog_get(&g_log, 9, &r));
}

static void test_garbage_last_record_is_kept_as_damaged(void)
{
    /* Not prefix-then-fill (e.g. a write that landed scrambled): keep it, burn its seq. */
    fresh_log(100, 3);
    uint8_t junk[EL_RECORD_SIZE];
    for (uint32_t i = 0; i < EL_RECORD_SIZE; i++) junk[i] = (uint8_t)(i * 37u + 11u);
    memcpy(g_mem + g_ram.size, junk, sizeof junk);
    g_ram.size += EL_RECORD_SIZE;
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    TEST_ASSERT_TRUE(g_rep.last_record_damaged);
    TEST_ASSERT_EQUAL_UINT32(4, el_datalog_count(&g_log));
    TEST_ASSERT_EQUAL_UINT32(5, append_ok());
}

static void test_damaged_first_and_last_record(void)
{
    fresh_log(100, 6);
    flip_bit(rec_off(0) + 50u, 0);
    flip_bit(rec_off(5) + 50u, 0);
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    TEST_ASSERT_EQUAL_UINT32(6, el_datalog_count(&g_log));
    TEST_ASSERT_EQUAL_UINT32(7, append_ok());
}

static void test_one_header_slot_damaged(void)
{
    for (uint32_t slot = 0; slot < 2; slot++) {
        fresh_log(100, 3);
        TEST_ASSERT_EQUAL(EL_OK, el_datalog_delete_all(&g_log, 1793500000));
        append_ok();                                    /* seq 4 */
        flip_bit(slot * 64u + 13u, 2);                  /* base_seq field */
        TEST_ASSERT_EQUAL(EL_OK, reopen(100));
        TEST_ASSERT_TRUE(g_rep.header_repaired);
        assert_log(4, 1);
        TEST_ASSERT_EQUAL(EL_OK, reopen(100));
        TEST_ASSERT_TRUE(report_clean(&g_rep));
    }
}

static void test_empty_log_after_delete_keeps_seq_with_one_slot_lost(void)
{
    /* No records to cross-check: the surviving slot alone must carry the new base. */
    fresh_log(100, 7);
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_delete_all(&g_log, 1793500000));
    for (uint32_t slot = 0; slot < 2; slot++) {
        memset(g_mem + slot * 64u, 0xFF, 64);
        TEST_ASSERT_EQUAL(EL_OK, reopen(100));
        assert_log(8, 0);
    }
}

static void test_stale_slot_never_lowers_the_base(void)
{
    /* Newest slot lost and the other holding an OLD base: the records' own seq wins. */
    fresh_log(100, 3);
    uint8_t old_hdr[EL_DATALOG_HEADER_SIZE];
    memcpy(old_hdr, g_mem, sizeof old_hdr);            /* base 1 */
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_delete_all(&g_log, 1793500000));
    append_ok(); append_ok();                           /* seq 4, 5 */
    memcpy(g_mem, old_hdr, 64);
    memset(g_mem + 64, 0, 64);
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    TEST_ASSERT_TRUE(g_rep.header_repaired);
    assert_log(4, 2);
    TEST_ASSERT_EQUAL_UINT32(6, append_ok());
}

static void test_both_header_slots_lost_recovered_from_records(void)
{
    fresh_log(100, 3);
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_delete_all(&g_log, 1793500000));
    append_ok(); append_ok(); append_ok();              /* seq 4..6 */
    memset(g_mem, 0, EL_DATALOG_HEADER_SIZE);
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    TEST_ASSERT_TRUE(g_rep.header_repaired);
    assert_log(4, 3);
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    TEST_ASSERT_TRUE(report_clean(&g_rep));

    /* ...even when the last record is damaged too (the first one tells the base). */
    memset(g_mem, 0xFF, EL_DATALOG_HEADER_SIZE);
    flip_bit(rec_off(2) + 30u, 4);
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    TEST_ASSERT_EQUAL_UINT32(4, el_datalog_first_seq(&g_log));
    TEST_ASSERT_EQUAL_UINT32(7, el_datalog_next_seq(&g_log));
}

static void test_foreign_file_is_left_untouched(void)
{
    ram_reset();
    for (uint32_t i = 0; i < 1000; i++) g_mem[i] = (uint8_t)(i * 7u);
    g_ram.size = 1000;
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, reopen(100));
    TEST_ASSERT_EQUAL_UINT32(1000, g_ram.size);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(999u * 7u), g_mem[999]);
    el_result_t r = result_for(1);
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_datalog_append(&g_log, &r, NULL));   /* not open */
}

static void test_header_from_newer_firmware_is_left_untouched(void)
{
    fresh_log(100, 2);
    for (uint32_t slot = 0; slot < 2; slot++) {
        g_mem[slot * 64u + 4u] = EL_DATALOG_VERSION + 1;
        el_put_u32le(g_mem + slot * 64u + 60u, el_crc32(g_mem + slot * 64u, 60));
    }
    uint32_t size = g_ram.size;
    TEST_ASSERT_EQUAL(EL_ERR_VERSION, reopen(100));
    TEST_ASSERT_EQUAL_UINT32(size, g_ram.size);
}

/* ---- (4) I/O errors -------------------------------------------------------------------------- */
static void test_read_errors(void)
{
    fresh_log(100, 20);
    g_ram.fail_size = true;
    TEST_ASSERT_EQUAL(IO_ERR, el_datalog_open(&g_log, &g_io, 100, 0, NULL));
    TEST_ASSERT_EQUAL_UINT32(0, el_datalog_count(&g_log));    /* not open after an error */
    for (uint32_t nth = 1; nth <= 2; nth++) {                 /* header read, last-record read */
        clear_faults();
        g_ram.reads = 0;
        g_ram.fail_read_from = nth;
        TEST_ASSERT_EQUAL(IO_ERR, el_datalog_open(&g_log, &g_io, 100, 0, NULL));
    }
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    el_result_t r;
    g_ram.reads = 0;
    g_ram.fail_read_from = 1;
    TEST_ASSERT_EQUAL(IO_ERR, el_datalog_get(&g_log, 3, &r));
    uint32_t seen = 0;
    el_datalog_iter_t st;
    g_ram.reads = 0;
    g_ram.fail_read_from = 2;                                 /* second batch fails */
    TEST_ASSERT_EQUAL(IO_ERR, el_datalog_iterate(&g_log, 0, UINT32_MAX, EL_DATALOG_OLDEST_FIRST, count_cb, &seen, &st));
    TEST_ASSERT_EQUAL_UINT32(EL_DATALOG_BATCH_RECORDS, seen);
    TEST_ASSERT_EQUAL_UINT32(EL_DATALOG_BATCH_RECORDS, st.examined);
    el_datalog_verify_t v;
    g_ram.reads = 0;
    TEST_ASSERT_EQUAL(IO_ERR, el_datalog_verify_all(&g_log, &v));
    clear_faults();
    assert_log(1, 20);                                        /* nothing was harmed */
}

static void test_append_errors_leave_state_unchanged(void)
{
    fresh_log(100, 5);
    el_result_t r = result_for(6);
    g_ram.fail_size = true;
    TEST_ASSERT_EQUAL(IO_ERR, el_datalog_append(&g_log, &r, NULL));
    clear_faults();
    g_ram.fail_append = true;
    TEST_ASSERT_EQUAL(IO_ERR, el_datalog_append(&g_log, &r, NULL));
    assert_log(1, 5);
    clear_faults();
    TEST_ASSERT_EQUAL_UINT32(6, append_ok());

    /* A partial write that did not lose power: the tail must be cut before the next append. */
    arm(50, CUT_FILL_00);
    r = result_for(7);
    TEST_ASSERT_EQUAL(IO_ERR, el_datalog_append(&g_log, &r, NULL));
    clear_faults();                                           /* device still alive */
    g_ram.fail_trunc = true;                                  /* ...and the repair fails once */
    TEST_ASSERT_EQUAL(IO_ERR, el_datalog_append(&g_log, &r, NULL));
    clear_faults();
    TEST_ASSERT_EQUAL_UINT32(7, append_ok());
    assert_log(1, 7);
    TEST_ASSERT_EQUAL_UINT32(rec_off(7), g_ram.size);
}

static void test_append_reported_failed_but_durable(void)
{
    /* fsync error after the data hit flash: not acknowledged, so it may vanish or appear. */
    fresh_log(100, 5);
    el_result_t r = result_for(6);
    g_ram.append_lies = true;
    TEST_ASSERT_EQUAL(IO_ERR, el_datalog_append(&g_log, &r, NULL));
    TEST_ASSERT_EQUAL_UINT32(5, el_datalog_count(&g_log));
    clear_faults();
    TEST_ASSERT_EQUAL_UINT32(6, append_ok());                 /* tail cut, seq never reported */
    assert_log(1, 6);
    TEST_ASSERT_EQUAL_UINT32(rec_off(6), g_ram.size);

    g_ram.append_lies = true;                                 /* same, then a reboot */
    r = result_for(7);
    TEST_ASSERT_EQUAL(IO_ERR, el_datalog_append(&g_log, &r, NULL));
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    assert_log(1, 7);
    TEST_ASSERT_EQUAL_UINT32(8, append_ok());
}

static void test_delete_errors(void)
{
    fresh_log(100, 5);
    g_ram.fail_write = true;                                  /* header write fails cleanly */
    TEST_ASSERT_EQUAL(IO_ERR, el_datalog_delete_all(&g_log, 1));
    clear_faults();
    assert_log(1, 5);
    TEST_ASSERT_EQUAL_UINT32(6, append_ok());                 /* re-reads the header first */
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    assert_log(1, 6);

    g_ram.write_lies = true;                                  /* header durable, error reported */
    TEST_ASSERT_EQUAL(IO_ERR, el_datalog_delete_all(&g_log, 1));
    clear_faults();
    TEST_ASSERT_EQUAL_UINT32(7, append_ok());                 /* learns the delete committed */
    assert_log(7, 1);
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));                    /* evens out the two slots */
    assert_log(7, 1);
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    TEST_ASSERT_TRUE(report_clean(&g_rep));

    g_ram.fail_trunc = true;                                  /* shrink fails: still a delete */
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_delete_all(&g_log, 2));
    assert_log(8, 0);
    clear_faults();
    TEST_ASSERT_EQUAL_UINT32(8, append_ok());                 /* finishes the shrink first */
    TEST_ASSERT_EQUAL_UINT32(rec_off(1), g_ram.size);
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    assert_log(8, 1);
}

/* ---- (5) Capacity ---------------------------------------------------------------------------- */
static void test_full_log_and_fill_level(void)
{
    fresh_log(10, 0);
    TEST_ASSERT_EQUAL_UINT32(0, el_datalog_fill_pct(&g_log));
    TEST_ASSERT_EQUAL_UINT32(10, el_datalog_capacity(&g_log));
    for (int i = 0; i < 5; i++) append_ok();
    TEST_ASSERT_EQUAL_UINT32(50, el_datalog_fill_pct(&g_log));
    append_ok(); append_ok(); append_ok();
    TEST_ASSERT_FALSE(el_datalog_near_full(&g_log));          /* 80 % */
    append_ok();
    TEST_ASSERT_TRUE(el_datalog_near_full(&g_log));           /* 90 % */
    TEST_ASSERT_FALSE(el_datalog_full(&g_log));
    append_ok();
    TEST_ASSERT_TRUE(el_datalog_full(&g_log));
    TEST_ASSERT_EQUAL_UINT32(100, el_datalog_fill_pct(&g_log));
    uint32_t size = g_ram.size, seq = 0;
    el_result_t r = result_for(11);
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_datalog_append(&g_log, &r, &seq));
    TEST_ASSERT_EQUAL_UINT32(0, seq);
    TEST_ASSERT_EQUAL_UINT32(size, g_ram.size);
    assert_log(1, 10);
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_delete_all(&g_log, 1793500000));
    TEST_ASSERT_EQUAL_UINT32(0, el_datalog_fill_pct(&g_log));
    TEST_ASSERT_EQUAL_UINT32(11, append_ok());

    /* Capacity lowered below the stored count: nothing is dropped, the log is just full. */
    fresh_log(100, 12);
    TEST_ASSERT_EQUAL(EL_OK, reopen(10));
    assert_log(1, 12);
    TEST_ASSERT_EQUAL_UINT32(100, el_datalog_fill_pct(&g_log));
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_datalog_append(&g_log, &r, NULL));
}

static void test_seq_continuity_across_deletes_and_reboots(void)
{
    fresh_log(100, 4);
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_delete_all(&g_log, 1793500000));
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_delete_all(&g_log, 1793500001));   /* empty delete */
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    assert_log(5, 0);
    TEST_ASSERT_EQUAL_UINT32(5, append_ok());
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_delete_all(&g_log, 1793500002));
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    TEST_ASSERT_TRUE(report_clean(&g_rep));
    TEST_ASSERT_EQUAL_UINT32(6, append_ok());
    TEST_ASSERT_EQUAL(EL_OK, reopen(100));
    assert_log(6, 1);
}

/* ---- (6) Iteration and search ----------------------------------------------------------------- */
typedef struct { uint32_t idx[64]; uint32_t n, stop_after; } visit_t;

static bool visit_cb(uint32_t index, const el_result_t *r, void *ctx)
{
    visit_t *v = ctx;
    TEST_ASSERT_EQUAL_UINT32(index + 1u, r->seq);              /* logs here start at seq 1 */
    v->idx[v->n++] = index;
    return v->stop_after == 0 || v->n < v->stop_after;
}

static void test_iterate_both_directions_with_early_stop(void)
{
    fresh_log(100, 20);
    visit_t v = {0};
    el_datalog_iter_t st;
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_iterate(&g_log, 0, UINT32_MAX, EL_DATALOG_OLDEST_FIRST, visit_cb, &v, &st));
    TEST_ASSERT_EQUAL_UINT32(20, v.n);
    for (uint32_t i = 0; i < 20; i++) TEST_ASSERT_EQUAL_UINT32(i, v.idx[i]);
    TEST_ASSERT_FALSE(st.stopped);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, st.first_bad_index);

    memset(&v, 0, sizeof v);                                   /* History: newest page of 5 */
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_iterate(&g_log, 19, 5, EL_DATALOG_NEWEST_FIRST, visit_cb, &v, &st));
    TEST_ASSERT_EQUAL_UINT32(5, v.n);
    for (uint32_t i = 0; i < 5; i++) TEST_ASSERT_EQUAL_UINT32(19u - i, v.idx[i]);
    memset(&v, 0, sizeof v);                                   /* next page resumes at 19 - 5 */
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_iterate(&g_log, 19u - st.examined, 5, EL_DATALOG_NEWEST_FIRST, visit_cb, &v, &st));
    TEST_ASSERT_EQUAL_UINT32(14, v.idx[0]);

    memset(&v, 0, sizeof v);                                   /* backwards to the very start */
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_iterate(&g_log, 10, UINT32_MAX, EL_DATALOG_NEWEST_FIRST, visit_cb, &v, &st));
    TEST_ASSERT_EQUAL_UINT32(11, v.n);
    TEST_ASSERT_EQUAL_UINT32(0, v.idx[10]);

    memset(&v, 0, sizeof v);
    v.stop_after = 3;
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_iterate(&g_log, 2, UINT32_MAX, EL_DATALOG_OLDEST_FIRST, visit_cb, &v, &st));
    TEST_ASSERT_EQUAL_UINT32(3, v.n);
    TEST_ASSERT_TRUE(st.stopped);
    TEST_ASSERT_EQUAL_UINT32(4, v.idx[2]);
    TEST_ASSERT_EQUAL_UINT32(3, st.examined);

    memset(&v, 0, sizeof v);
    v.stop_after = 2;
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_iterate(&g_log, 19, UINT32_MAX, EL_DATALOG_NEWEST_FIRST, visit_cb, &v, &st));
    TEST_ASSERT_EQUAL_UINT32(2, v.n);
    TEST_ASSERT_EQUAL_UINT32(18, v.idx[1]);

    memset(&v, 0, sizeof v);                                   /* edge cases visit nothing */
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_iterate(&g_log, 20, 5, EL_DATALOG_OLDEST_FIRST, visit_cb, &v, &st));
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_iterate(&g_log, 0, 0, EL_DATALOG_OLDEST_FIRST, visit_cb, &v, &st));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_datalog_iterate(&g_log, 0, 5, (el_datalog_dir_t)7, visit_cb, &v, &st));
    TEST_ASSERT_EQUAL_UINT32(0, v.n);
    fresh_log(100, 0);
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_iterate(&g_log, 0, 5, EL_DATALOG_NEWEST_FIRST, visit_cb, &v, &st));
    TEST_ASSERT_EQUAL_UINT32(0, v.n);
}

static void test_find_first_after_seq(void)
{
    uint32_t idx = 99;
    fresh_log(100, 0);                                         /* empty log */
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_find_first_after_seq(&g_log, 0, &idx));
    TEST_ASSERT_EQUAL_UINT32(0, idx);
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_find_first_after_seq(&g_log, 1000, &idx));
    TEST_ASSERT_EQUAL_UINT32(0, idx);

    for (int i = 0; i < 10; i++) append_ok();                  /* seq 1..10 */
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_delete_all(&g_log, 1793500000));
    for (int i = 0; i < 10; i++) append_ok();                  /* seq 11..20 at index 0..9 */
    el_datalog_find_first_after_seq(&g_log, 0, &idx);  TEST_ASSERT_EQUAL_UINT32(0, idx);
    el_datalog_find_first_after_seq(&g_log, 5, &idx);  TEST_ASSERT_EQUAL_UINT32(0, idx);   /* before first */
    el_datalog_find_first_after_seq(&g_log, 10, &idx); TEST_ASSERT_EQUAL_UINT32(0, idx);
    el_datalog_find_first_after_seq(&g_log, 11, &idx); TEST_ASSERT_EQUAL_UINT32(1, idx);
    el_datalog_find_first_after_seq(&g_log, 15, &idx); TEST_ASSERT_EQUAL_UINT32(5, idx);
    assert_record(idx, 16);
    el_datalog_find_first_after_seq(&g_log, 19, &idx); TEST_ASSERT_EQUAL_UINT32(9, idx);
    el_datalog_find_first_after_seq(&g_log, 20, &idx); TEST_ASSERT_EQUAL_UINT32(10, idx);  /* last: none */
    el_datalog_find_first_after_seq(&g_log, UINT32_MAX, &idx); TEST_ASSERT_EQUAL_UINT32(10, idx);
}

/* ---- (7) 50,000 records ---------------------------------------------------------------------- */
static void test_fifty_thousand_records_open_in_constant_reads(void)
{
    fresh_log(BIG_RECORDS, 0);
    for (uint32_t i = 0; i < BIG_RECORDS; i++) append_ok();
    TEST_ASSERT_EQUAL_UINT32(rec_off(BIG_RECORDS), g_ram.size);   /* 6.4 MB */

    TEST_ASSERT_EQUAL(EL_OK, reopen(BIG_RECORDS));
    TEST_ASSERT_TRUE(report_clean(&g_rep));
    TEST_ASSERT_EQUAL_UINT32(BIG_RECORDS, g_rep.records);
    TEST_ASSERT_TRUE(g_ram.reads <= 3);                        /* header + last (+ first) */
    TEST_ASSERT_TRUE(g_ram.read_bytes <= 3u * 128u);
    TEST_ASSERT_TRUE(el_datalog_full(&g_log));
    TEST_ASSERT_TRUE(el_datalog_near_full(&g_log));

    flip_bit(rec_off(BIG_RECORDS - 1u) + 60u, 0);              /* damaged last: one extra read */
    g_ram.size += 77;                                          /* plus a partial tail */
    TEST_ASSERT_EQUAL(EL_OK, reopen(BIG_RECORDS));
    TEST_ASSERT_TRUE(g_ram.reads <= 3);
    TEST_ASSERT_EQUAL_UINT32(77, g_rep.bytes_truncated);
    flip_bit(rec_off(BIG_RECORDS - 1u) + 60u, 0);

    assert_record(0, 1);
    assert_record(25000, 25001);
    assert_record(BIG_RECORDS - 1u, BIG_RECORDS);
    uint32_t idx;
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_find_first_after_seq(&g_log, 40000, &idx));
    TEST_ASSERT_EQUAL_UINT32(40000, idx);
    assert_record(idx, 40001);

    visit_t v = {0};
    el_datalog_iter_t st;
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_iterate(&g_log, BIG_RECORDS - 1u, 20, EL_DATALOG_NEWEST_FIRST, visit_cb, &v, &st));
    TEST_ASSERT_EQUAL_UINT32(20, v.n);
    TEST_ASSERT_EQUAL_UINT32(BIG_RECORDS - 20u, v.idx[19]);

    el_datalog_verify_t ver;
    TEST_ASSERT_EQUAL(EL_OK, el_datalog_verify_all(&g_log, &ver));
    TEST_ASSERT_EQUAL_UINT32(BIG_RECORDS, ver.good);
    TEST_ASSERT_EQUAL_UINT32(0, ver.bad);
}

void run_datalog_tests(void)
{
    RUN_TEST(test_new_log_is_created_and_persists);
    RUN_TEST(test_records_sit_at_fixed_offsets);
    RUN_TEST(test_bad_arguments);
    RUN_TEST(test_bad_status_is_refused_without_writing);
    RUN_TEST(test_power_cut_during_append_at_every_byte);
    RUN_TEST(test_cut_append_reports_what_was_removed);
    RUN_TEST(test_power_cut_during_delete_all_at_every_byte);
    RUN_TEST(test_delete_cut_before_shrink_is_finished_on_open);
    RUN_TEST(test_delete_shrink_cut_halfway_is_finished_on_open);
    RUN_TEST(test_power_cut_during_header_creation_at_every_byte);
    RUN_TEST(test_power_cut_during_open_repair);
    RUN_TEST(test_random_power_cut_torture);
    RUN_TEST(test_bit_flip_in_middle_record);
    RUN_TEST(test_bit_flip_in_last_record_keeps_it_and_its_seq);
    RUN_TEST(test_garbage_last_record_is_kept_as_damaged);
    RUN_TEST(test_damaged_first_and_last_record);
    RUN_TEST(test_one_header_slot_damaged);
    RUN_TEST(test_empty_log_after_delete_keeps_seq_with_one_slot_lost);
    RUN_TEST(test_stale_slot_never_lowers_the_base);
    RUN_TEST(test_both_header_slots_lost_recovered_from_records);
    RUN_TEST(test_foreign_file_is_left_untouched);
    RUN_TEST(test_header_from_newer_firmware_is_left_untouched);
    RUN_TEST(test_read_errors);
    RUN_TEST(test_append_errors_leave_state_unchanged);
    RUN_TEST(test_append_reported_failed_but_durable);
    RUN_TEST(test_delete_errors);
    RUN_TEST(test_full_log_and_fill_level);
    RUN_TEST(test_seq_continuity_across_deletes_and_reboots);
    RUN_TEST(test_iterate_both_directions_with_early_stop);
    RUN_TEST(test_find_first_after_seq);
    RUN_TEST(test_fifty_thousand_records_open_in_constant_reads);
}
