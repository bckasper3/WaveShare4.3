#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "el_crc.h"
#include "el_store.h"
#include "el_settings.h"
#include "el_util.h"

/* ---- Two NVS keys modelled as byte buffers. A power cut while writing a slot leaves the key
 * as one of: absent/short (a prefix of the new bytes), the new prefix followed by the old bytes,
 * or the new prefix followed by 0xFF / 0x00 fill. Real NVS is atomic per key, so this is a
 * strictly harsher model than the medium. ---------------------------------------------------- */
#define SLOT_CAP 512

typedef struct {
    uint8_t  b[2][SLOT_CAP];
    size_t   len[2];
} keys_t;

typedef enum { TORN_SHORT, TORN_OLD_TAIL, TORN_FF, TORN_00, TORN_MODES } torn_t;

static void key_write(keys_t *k, uint8_t slot, const uint8_t *data, size_t n)
{
    memcpy(k->b[slot], data, n);
    k->len[slot] = n;
}

/* Write only the first `cut` bytes of data into slot, completing the rest per mode. */
static void key_write_torn(keys_t *k, uint8_t slot, const uint8_t *data, size_t n, size_t cut, torn_t mode)
{
    uint8_t old[SLOT_CAP];
    size_t old_len = k->len[slot];
    memcpy(old, k->b[slot], old_len);
    memcpy(k->b[slot], data, cut);
    switch (mode) {
    case TORN_SHORT: k->len[slot] = cut; break;
    case TORN_OLD_TAIL:
        for (size_t i = cut; i < n; i++) k->b[slot][i] = i < old_len ? old[i] : 0xFF;
        k->len[slot] = n; break;
    case TORN_FF: memset(k->b[slot] + cut, 0xFF, n - cut); k->len[slot] = n; break;
    default:      memset(k->b[slot] + cut, 0x00, n - cut); k->len[slot] = n; break;
    }
}

static el_err_t select_u32(const keys_t *k, el_store_pair_t *p, uint32_t *val)
{
    const uint8_t *pl = NULL;
    size_t pn = 0;
    el_err_t e = el_store_pair_select(p, EL_STORE_TAG_EXPORT_WM, k->b[0], k->len[0], k->b[1], k->len[1],
                                      NULL, NULL, &pl, &pn);
    if (e == EL_OK) {
        TEST_ASSERT_EQUAL_UINT(4, pn);
        *val = el_get_u32le(pl);
    }
    return e;
}

/* Full write of a u32 value through the pair (what storage does: older slot, then commit). */
static void write_u32(keys_t *k, el_store_pair_t *p, uint32_t v)
{
    uint8_t pay[4], img[SLOT_CAP];
    size_t n;
    uint8_t slot; uint32_t gen;
    el_put_u32le(pay, v);
    el_store_pair_next(p, &slot, &gen);
    TEST_ASSERT_EQUAL(EL_OK, el_store_slot_encode(EL_STORE_TAG_EXPORT_WM, gen, pay, 4, img, sizeof img, &n));
    key_write(k, slot, img, n);
    el_store_pair_written(p, slot, gen, true);
}

/* ---------------------------------------------------------------------------------------- */

static void test_slot_roundtrip_and_layout(void)
{
    uint8_t img[64];
    size_t n = 0;
    const uint8_t pay[3] = { 1, 2, 3 };
    TEST_ASSERT_EQUAL(EL_OK, el_store_slot_encode(EL_STORE_TAG_MIRROR_WM, 7, pay, 3, img, sizeof img, &n));
    TEST_ASSERT_EQUAL_UINT(EL_STORE_SLOT_OVERHEAD + 3, n);
    TEST_ASSERT_EQUAL_HEX8(0x3C, img[0]);        /* magic, little-endian */
    TEST_ASSERT_EQUAL_HEX8(0x5A, img[1]);
    TEST_ASSERT_EQUAL_UINT8(1, img[2]);
    TEST_ASSERT_EQUAL_UINT8(EL_STORE_TAG_MIRROR_WM, img[3]);
    TEST_ASSERT_EQUAL_UINT32(7, el_get_u32le(img + 4));
    TEST_ASSERT_EQUAL_UINT16(3, el_get_u16le(img + 8));

    uint32_t gen = 0; const uint8_t *pl = NULL; size_t pn = 0;
    TEST_ASSERT_EQUAL(EL_OK, el_store_slot_decode(img, n, EL_STORE_TAG_MIRROR_WM, &gen, &pl, &pn));
    TEST_ASSERT_EQUAL_UINT32(7, gen);
    TEST_ASSERT_EQUAL_UINT(3, pn);
    TEST_ASSERT_EQUAL_MEMORY(pay, pl, 3);

    /* empty payload is legal (a blob that is just "present") */
    TEST_ASSERT_EQUAL(EL_OK, el_store_slot_encode(EL_STORE_TAG_MIRROR_WM, 1, NULL, 0, img, sizeof img, &n));
    TEST_ASSERT_EQUAL(EL_OK, el_store_slot_decode(img, n, EL_STORE_TAG_MIRROR_WM, NULL, NULL, &pn));
    TEST_ASSERT_EQUAL_UINT(0, pn);
}

static void test_slot_encode_errors(void)
{
    uint8_t img[32]; size_t n;
    uint8_t pay[20] = { 0 };
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_store_slot_encode(1, 0, pay, 4, img, sizeof img, &n));   /* gen 0 */
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_store_slot_encode(1, 1, NULL, 4, img, sizeof img, &n));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_store_slot_encode(1, 1, pay, 4, NULL, 0, &n));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_store_slot_encode(1, 1, pay, 0x10000, img, sizeof img, &n));
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_store_slot_encode(1, 1, pay, 20, img, sizeof img, &n));  /* 36 > 32 */
    TEST_ASSERT_EQUAL(EL_OK, el_store_slot_encode(1, 1, pay, 16, img, sizeof img, &n));       /* exactly 32 */
}

static void test_slot_decode_rejects(void)
{
    uint8_t img[64], bad[64]; size_t n;
    const uint8_t pay[8] = { 9, 8, 7, 6, 5, 4, 3, 2 };
    el_store_slot_encode(EL_STORE_TAG_UPLINK_WM, 5, pay, 8, img, sizeof img, &n);

    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_store_slot_decode(NULL, 0, EL_STORE_TAG_UPLINK_WM, NULL, NULL, NULL));
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_store_slot_decode(img, EL_STORE_SLOT_OVERHEAD - 1, EL_STORE_TAG_UPLINK_WM, NULL, NULL, NULL));
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_store_slot_decode(img, n - 1, EL_STORE_TAG_UPLINK_WM, NULL, NULL, NULL));
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_store_slot_decode(img, n, EL_STORE_TAG_MIRROR_WM, NULL, NULL, NULL));  /* other blob */
    memcpy(bad, img, n); bad[n] = 0;
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_store_slot_decode(bad, n + 1, EL_STORE_TAG_UPLINK_WM, NULL, NULL, NULL)); /* trailing */
    memcpy(bad, img, n); bad[0] ^= 1;
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_store_slot_decode(bad, n, EL_STORE_TAG_UPLINK_WM, NULL, NULL, NULL));
    memcpy(bad, img, n); bad[EL_STORE_SLOT_HDR + 3] ^= 0x10;                       /* bit flip */
    TEST_ASSERT_EQUAL(EL_ERR_CRC, el_store_slot_decode(bad, n, EL_STORE_TAG_UPLINK_WM, NULL, NULL, NULL));
    memset(bad, 0xFF, n);                                                          /* erased */
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_store_slot_decode(bad, n, EL_STORE_TAG_UPLINK_WM, NULL, NULL, NULL));

    /* newer wrapper version with a correct CRC: VERSION (not CRC) */
    memcpy(bad, img, n); bad[2] = 2;
    el_store_slot_encode(EL_STORE_TAG_UPLINK_WM, 5, pay, 8, bad, sizeof bad, &n);
    bad[2] = 2;
    el_put_u32le(bad + EL_STORE_SLOT_HDR + 8, el_crc32(bad, EL_STORE_SLOT_HDR + 8));
    TEST_ASSERT_EQUAL(EL_ERR_VERSION, el_store_slot_decode(bad, n, EL_STORE_TAG_UPLINK_WM, NULL, NULL, NULL));
}

static void test_gen_newer_wraps(void)
{
    TEST_ASSERT_TRUE(el_store_gen_newer(2, 1));
    TEST_ASSERT_FALSE(el_store_gen_newer(1, 2));
    TEST_ASSERT_FALSE(el_store_gen_newer(5, 5));
    TEST_ASSERT_TRUE(el_store_gen_newer(1, 0xFFFFFFFFu));      /* after wrap */
    TEST_ASSERT_FALSE(el_store_gen_newer(0xFFFFFFFFu, 1));
}

static void test_pair_fresh_device_and_first_writes(void)
{
    keys_t k; memset(&k, 0, sizeof k);
    el_store_pair_t p;
    uint32_t v = 99;
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, select_u32(&k, &p, &v));  /* nothing stored */
    TEST_ASSERT_EQUAL_INT8(-1, p.current);
    TEST_ASSERT_EQUAL_UINT32(99, v);

    uint8_t slot; uint32_t gen;
    el_store_pair_next(&p, &slot, &gen);
    TEST_ASSERT_EQUAL_UINT8(0, slot);
    TEST_ASSERT_EQUAL_UINT32(1, gen);

    write_u32(&k, &p, 10);       /* slot 0, gen 1 */
    write_u32(&k, &p, 11);       /* slot 1, gen 2 */
    write_u32(&k, &p, 12);       /* slot 0, gen 3 */
    TEST_ASSERT_EQUAL_UINT32(3, el_get_u32le(k.b[0] + 4));
    TEST_ASSERT_EQUAL_UINT32(2, el_get_u32le(k.b[1] + 4));

    el_store_pair_t q;                                          /* "reboot" */
    TEST_ASSERT_EQUAL(EL_OK, select_u32(&k, &q, &v));
    TEST_ASSERT_EQUAL_UINT32(12, v);
    TEST_ASSERT_EQUAL_INT8(0, q.current);
    el_store_pair_next(&q, &slot, &gen);
    TEST_ASSERT_EQUAL_UINT8(1, slot);                           /* never the slot in use */
    TEST_ASSERT_EQUAL_UINT32(4, gen);
}

static void test_pair_both_damaged_is_crc(void)
{
    keys_t k; memset(&k, 0, sizeof k);
    el_store_pair_t p; el_store_pair_init(&p);
    write_u32(&k, &p, 1);
    write_u32(&k, &p, 2);
    k.b[0][EL_STORE_SLOT_HDR] ^= 1;
    k.b[1][EL_STORE_SLOT_HDR] ^= 1;
    uint32_t v;
    TEST_ASSERT_EQUAL(EL_ERR_CRC, select_u32(&k, &p, &v));
    TEST_ASSERT_EQUAL_INT8(-1, p.current);
    /* one damaged: the other is used even though it is older */
    k.b[0][EL_STORE_SLOT_HDR] ^= 1;
    TEST_ASSERT_EQUAL(EL_OK, select_u32(&k, &p, &v));
    TEST_ASSERT_EQUAL_UINT32(1, v);
}

static void test_pair_generation_wrap(void)
{
    keys_t k; memset(&k, 0, sizeof k);
    uint8_t img[64]; size_t n; uint8_t pay[4];
    el_put_u32le(pay, 100);
    el_store_slot_encode(EL_STORE_TAG_EXPORT_WM, 0xFFFFFFFFu, pay, 4, img, sizeof img, &n);
    key_write(&k, 0, img, n);
    el_put_u32le(pay, 99);
    el_store_slot_encode(EL_STORE_TAG_EXPORT_WM, 0xFFFFFFFEu, pay, 4, img, sizeof img, &n);
    key_write(&k, 1, img, n);

    el_store_pair_t p; uint32_t v;
    TEST_ASSERT_EQUAL(EL_OK, select_u32(&k, &p, &v));
    TEST_ASSERT_EQUAL_UINT32(100, v);
    uint8_t slot; uint32_t gen;
    el_store_pair_next(&p, &slot, &gen);
    TEST_ASSERT_EQUAL_UINT8(1, slot);
    TEST_ASSERT_EQUAL_UINT32(1, gen);                 /* skips 0 */
    write_u32(&k, &p, 101);
    TEST_ASSERT_EQUAL(EL_OK, select_u32(&k, &p, &v));
    TEST_ASSERT_EQUAL_UINT32(101, v);                 /* gen 1 is newer than 0xFFFFFFFF */
}

static void test_pair_failed_write_retries_same_slot(void)
{
    keys_t k; memset(&k, 0, sizeof k);
    el_store_pair_t p; el_store_pair_init(&p);
    write_u32(&k, &p, 5);                             /* slot 0 gen 1 current */
    uint8_t slot, slot2; uint32_t gen, gen2;
    el_store_pair_next(&p, &slot, &gen);
    TEST_ASSERT_EQUAL_UINT8(1, slot);
    /* NVS commit reported an error, but the value may have landed: write it, report failure */
    uint8_t img[64]; size_t n; uint8_t pay[4]; el_put_u32le(pay, 6);
    el_store_slot_encode(EL_STORE_TAG_EXPORT_WM, gen, pay, 4, img, sizeof img, &n);
    key_write(&k, slot, img, n);
    el_store_pair_written(&p, slot, gen, false);
    TEST_ASSERT_EQUAL_INT8(0, p.current);
    el_store_pair_next(&p, &slot2, &gen2);
    TEST_ASSERT_EQUAL_UINT8(1, slot2);                /* the good copy in slot 0 is never touched */
    TEST_ASSERT_EQUAL_UINT32(gen, gen2);

    /* failing a write to the CURRENT slot (cannot happen through next(), defensive) */
    el_store_pair_written(&p, 0, 9, false);
    TEST_ASSERT_EQUAL_INT8(-1, p.current);
    el_store_pair_written(&p, 2, 1, true);            /* bad slot ignored */
    TEST_ASSERT_EQUAL_INT8(-1, p.current);
}

/* Accept callback: wraps the real settings codec, as storage does. */
static bool accept_settings(const uint8_t *pl, size_t n, void *ctx)
{
    el_settings_t s;
    (void)ctx;
    return el_settings_decode(pl, n, &s) == EL_OK;
}

static void put_settings(keys_t *k, uint8_t slot, uint32_t gen, const el_settings_t *s, bool newer_version)
{
    uint8_t pay[EL_SETTINGS_ENCODED_MAX], img[SLOT_CAP];
    size_t pn, n;
    TEST_ASSERT_EQUAL(EL_OK, el_settings_encode(s, pay, sizeof pay, &pn));
    if (newer_version) pay[2] = EL_SETTINGS_VERSION + 1;   /* inner CRC now wrong too: rejected */
    TEST_ASSERT_EQUAL(EL_OK, el_store_slot_encode(EL_STORE_TAG_SETTINGS, gen, pay, pn, img, sizeof img, &n));
    key_write(k, slot, img, n);
}

static void test_pair_rejected_newest_falls_back_and_is_not_overwritten_blindly(void)
{
    keys_t k; memset(&k, 0, sizeof k);
    el_settings_t s; el_settings_defaults(&s);
    s.mb_addr = 17;
    put_settings(&k, 0, 41, &s, false);
    s.mb_addr = 18;
    put_settings(&k, 1, 42, &s, true);           /* e.g. written by newer firmware, then downgraded */

    el_store_pair_t p;
    const uint8_t *pl; size_t pn;
    TEST_ASSERT_EQUAL(EL_OK, el_store_pair_select(&p, EL_STORE_TAG_SETTINGS, k.b[0], k.len[0],
                                                  k.b[1], k.len[1], accept_settings, NULL, &pl, &pn));
    TEST_ASSERT_EQUAL_INT8(0, p.current);
    el_settings_t got;
    TEST_ASSERT_EQUAL(EL_OK, el_settings_decode(pl, pn, &got));
    TEST_ASSERT_EQUAL_UINT8(17, got.mb_addr);
    uint8_t slot; uint32_t gen;
    el_store_pair_next(&p, &slot, &gen);
    TEST_ASSERT_EQUAL_UINT8(1, slot);
    TEST_ASSERT_EQUAL_UINT32(43, gen);           /* above the rejected copy */

    /* both rejected: defaults, and the next write keeps the newer copy */
    put_settings(&k, 0, 41, &s, true);
    TEST_ASSERT_EQUAL(EL_ERR_CRC, el_store_pair_select(&p, EL_STORE_TAG_SETTINGS, k.b[0], k.len[0],
                                                       k.b[1], k.len[1], accept_settings, NULL, &pl, &pn));
    el_store_pair_next(&p, &slot, &gen);
    TEST_ASSERT_EQUAL_UINT8(0, slot);
    TEST_ASSERT_EQUAL_UINT32(43, gen);
}

/* Power cut at every byte of every write of a sequence, with every torn shape: the value read
 * back is always the last acknowledged one, or the interrupted one if all its bytes landed. */
static void test_pair_power_cut_every_byte(void)
{
    el_settings_t vals[4];
    for (int i = 0; i < 4; i++) {
        el_settings_defaults(&vals[i]);
        vals[i].mb_addr = (uint8_t)(10 + i);
        snprintf(vals[i].device_serial, sizeof vals[i].device_serial, "EL-%04d", i * 1111);
    }
    unsigned checked = 0;
    for (int step = 1; step < 4; step++) {
        uint8_t pay[EL_SETTINGS_ENCODED_MAX], img[SLOT_CAP];
        size_t pn, n;
        el_settings_encode(&vals[step], pay, sizeof pay, &pn);
        for (torn_t mode = 0; mode < TORN_MODES; mode++) {
            /* state before the write: vals[0..step-1] written cleanly */
            keys_t base; memset(&base, 0, sizeof base);
            el_store_pair_t p; el_store_pair_init(&p);
            for (int j = 0; j < step; j++) {
                uint8_t bp[EL_SETTINGS_ENCODED_MAX], bi[SLOT_CAP]; size_t bpn, bn;
                uint8_t slot; uint32_t gen;
                el_settings_encode(&vals[j], bp, sizeof bp, &bpn);
                el_store_pair_next(&p, &slot, &gen);
                el_store_slot_encode(EL_STORE_TAG_SETTINGS, gen, bp, bpn, bi, sizeof bi, &bn);
                key_write(&base, slot, bi, bn);
                el_store_pair_written(&p, slot, gen, true);
            }
            uint8_t slot; uint32_t gen;
            el_store_pair_next(&p, &slot, &gen);
            el_store_slot_encode(EL_STORE_TAG_SETTINGS, gen, pay, pn, img, sizeof img, &n);
            for (size_t cut = 0; cut <= n; cut++) {
                keys_t k = base;
                if (cut == n) key_write(&k, slot, img, n);
                else key_write_torn(&k, slot, img, n, cut, mode);
                el_store_pair_t q; const uint8_t *pl; size_t pln;
                TEST_ASSERT_EQUAL(EL_OK, el_store_pair_select(&q, EL_STORE_TAG_SETTINGS, k.b[0], k.len[0],
                                                              k.b[1], k.len[1], accept_settings, NULL, &pl, &pln));
                el_settings_t got;
                TEST_ASSERT_EQUAL(EL_OK, el_settings_decode(pl, pln, &got));
                /* a torn tail can by chance equal the new bytes: then the write did land */
                bool landed = k.len[slot] == n && memcmp(k.b[slot], img, n) == 0;
                uint8_t want = landed ? vals[step].mb_addr : vals[step - 1].mb_addr;
                TEST_ASSERT_EQUAL_UINT8(want, got.mb_addr);
                /* and the next write after reboot never targets the copy just read */
                uint8_t ns; el_store_pair_next(&q, &ns, NULL);
                TEST_ASSERT_NOT_EQUAL(q.current, ns);
                checked++;
            }
        }
    }
    TEST_ASSERT_TRUE(checked > 1000);
}

/* Random torture: 4000 writes of a counter, a cut (random byte, random shape) on ~1 in 3,
 * reboot after each cut. The counter read back never goes below the last acknowledged value
 * and never shows a value that was not completely written. */
static uint32_t rng_state = 12345u;
static uint32_t rnd(void) { rng_state = rng_state * 1103515245u + 12345u; return rng_state >> 8; }

static void test_pair_random_torture(void)
{
    keys_t k; memset(&k, 0, sizeof k);
    el_store_pair_t p; el_store_pair_init(&p);
    uint32_t acked = 0, value = 0;
    for (int i = 0; i < 4000; i++) {
        uint32_t v = value + 1;
        uint8_t pay[4], img[64]; size_t n;
        uint8_t slot; uint32_t gen;
        el_put_u32le(pay, v);
        el_store_pair_next(&p, &slot, &gen);
        el_store_slot_encode(EL_STORE_TAG_EXPORT_WM, gen, pay, 4, img, sizeof img, &n);
        if (rnd() % 3 == 0) {
            size_t cut = rnd() % n;
            key_write_torn(&k, slot, img, n, cut, (torn_t)(rnd() % TORN_MODES));
            if (k.len[slot] == n && memcmp(k.b[slot], img, n) == 0) acked = v;   /* landed anyway */
            uint32_t got = 0;
            el_err_t e = select_u32(&k, &p, &got);           /* reboot */
            if (acked == 0) {
                TEST_ASSERT_TRUE(e != EL_OK || got == acked);
                if (e != EL_OK) got = 0;
            } else {
                TEST_ASSERT_EQUAL(EL_OK, e);
            }
            TEST_ASSERT_EQUAL_UINT32(acked, got);
            value = got;
        } else {
            key_write(&k, slot, img, n);
            el_store_pair_written(&p, slot, gen, true);
            acked = value = v;
        }
    }
    uint32_t got;
    TEST_ASSERT_EQUAL(EL_OK, select_u32(&k, &p, &got));
    TEST_ASSERT_EQUAL_UINT32(acked, got);
    TEST_ASSERT_TRUE(acked > 2000);
}

/* A counter (here the export watermark) handed out only after the write was acknowledged is never reused, even when
 * the write of the NEXT number is cut. */
static void test_counter_never_reused(void)
{
    keys_t k; memset(&k, 0, sizeof k);
    el_store_pair_t p; el_store_pair_init(&p);
    uint32_t handed_max = 0;
    for (int i = 0; i < 300; i++) {
        uint32_t cur = 0;
        if (select_u32(&k, &p, &cur) != EL_OK) cur = 0;      /* boot */
        TEST_ASSERT_TRUE(cur >= handed_max);
        uint32_t nxt = cur + 1;
        uint8_t pay[4], img[64]; size_t n; uint8_t slot; uint32_t gen;
        el_put_u32le(pay, nxt);
        el_store_pair_next(&p, &slot, &gen);
        el_store_slot_encode(EL_STORE_TAG_EXPORT_WM, gen, pay, 4, img, sizeof img, &n);
        if (i % 2) {
            key_write_torn(&k, slot, img, n, (size_t)i % n, (torn_t)(i % TORN_MODES));   /* cut: not handed out */
        } else {
            key_write(&k, slot, img, n);
            el_store_pair_written(&p, slot, gen, true);
            handed_max = nxt;
        }
    }
}

void run_store_slot_tests(void)
{
    RUN_TEST(test_slot_roundtrip_and_layout);
    RUN_TEST(test_slot_encode_errors);
    RUN_TEST(test_slot_decode_rejects);
    RUN_TEST(test_gen_newer_wraps);
    RUN_TEST(test_pair_fresh_device_and_first_writes);
    RUN_TEST(test_pair_both_damaged_is_crc);
    RUN_TEST(test_pair_generation_wrap);
    RUN_TEST(test_pair_failed_write_retries_same_slot);
    RUN_TEST(test_pair_rejected_newest_falls_back_and_is_not_overwritten_blindly);
    RUN_TEST(test_pair_power_cut_every_byte);
    RUN_TEST(test_pair_random_torture);
    RUN_TEST(test_counter_never_reused);
}
