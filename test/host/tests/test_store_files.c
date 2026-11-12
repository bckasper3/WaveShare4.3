#include <string.h>
#include "unity.h"
#include "el_store.h"

/* 2026-10-30T12:34:56Z */
#define T_REF 1793363696

static void test_safe_serial(void)
{
    char b[16];
    TEST_ASSERT_EQUAL(EL_OK, el_store_safe_serial(b, sizeof b, "EL-0001"));
    TEST_ASSERT_EQUAL_STRING("EL-0001", b);
    TEST_ASSERT_EQUAL(EL_OK, el_store_safe_serial(b, sizeof b, "A/B\\C:D*E?F\"G<H>|. "));
    TEST_ASSERT_EQUAL_STRING("A_B_C_D_E_F_G_H", b);           /* 15 chars max */
    TEST_ASSERT_EQUAL(EL_OK, el_store_safe_serial(b, sizeof b, ""));
    TEST_ASSERT_EQUAL_STRING("UNSET", b);
    TEST_ASSERT_EQUAL(EL_OK, el_store_safe_serial(b, sizeof b, NULL));
    TEST_ASSERT_EQUAL_STRING("UNSET", b);
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_store_safe_serial(b, 4, "EL-0001"));
    TEST_ASSERT_EQUAL_STRING("EL-", b);
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_store_safe_serial(NULL, 4, "x"));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_store_safe_serial(b, 0, "x"));
}

static void test_export_and_mirror_names(void)
{
    char b[EL_STORE_NAME_MAX];
    TEST_ASSERT_EQUAL(EL_OK, el_store_export_name(b, sizeof b, "EL-0001", T_REF, 0));
    TEST_ASSERT_EQUAL_STRING("ENVLOG_EL-0001_20261030-123456.csv", b);
    TEST_ASSERT_EQUAL(EL_OK, el_store_export_name(b, sizeof b, "EL-0001", T_REF, 2));
    TEST_ASSERT_EQUAL_STRING("ENVLOG_EL-0001_20261030-123456_2.csv", b);
    TEST_ASSERT_EQUAL(EL_OK, el_store_mirror_name(b, sizeof b, "EL-0001", T_REF));
    TEST_ASSERT_EQUAL_STRING("ENVLOG_EL-0001_20261030.csv", b);
    TEST_ASSERT_EQUAL(EL_OK, el_store_mirror_name(b, sizeof b, "EL 1", -5));   /* clock never set */
    TEST_ASSERT_EQUAL_STRING("ENVLOG_EL_1_19700101.csv", b);
    /* the longest possible name fits the documented buffer */
    TEST_ASSERT_EQUAL(EL_OK, el_store_export_name(b, sizeof b, "ABCDEFGHIJKLMNO", T_REF, 65535));
    TEST_ASSERT_TRUE(strlen(b) < EL_STORE_NAME_MAX);
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_store_export_name(b, 20, "EL-0001", T_REF, 0));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_store_mirror_name(NULL, 20, "EL", T_REF));
}

static void test_export_range(void)
{
    uint32_t f, e;
    /* log holds seq 101..500 (next 501) */
    TEST_ASSERT_EQUAL(EL_OK, el_store_export_range(EL_STORE_EXP_ALL, 300, 101, 501, &f, &e));
    TEST_ASSERT_EQUAL_UINT32(101, f); TEST_ASSERT_EQUAL_UINT32(501, e);
    TEST_ASSERT_EQUAL(EL_OK, el_store_export_range(EL_STORE_EXP_SINCE_LAST, 300, 101, 501, &f, &e));
    TEST_ASSERT_EQUAL_UINT32(301, f);
    TEST_ASSERT_EQUAL(EL_OK, el_store_export_range(EL_STORE_EXP_SINCE_LAST, 500, 101, 501, &f, &e));
    TEST_ASSERT_EQUAL_UINT32(e, f);                                 /* nothing new */
    TEST_ASSERT_EQUAL(EL_OK, el_store_export_range(EL_STORE_EXP_SINCE_LAST, 0, 101, 501, &f, &e));
    TEST_ASSERT_EQUAL_UINT32(101, f);                               /* never exported */
    TEST_ASSERT_EQUAL(EL_OK, el_store_export_range(EL_STORE_EXP_SINCE_LAST, 50, 101, 501, &f, &e));
    TEST_ASSERT_EQUAL_UINT32(101, f);                               /* older ones deleted */
    TEST_ASSERT_EQUAL(EL_OK, el_store_export_range(EL_STORE_EXP_SINCE_LAST, 9999, 101, 501, &f, &e));
    TEST_ASSERT_EQUAL_UINT32(101, f);                               /* impossible watermark */
    TEST_ASSERT_EQUAL(EL_OK, el_store_export_range(EL_STORE_EXP_ALL, 0, 1, 1, &f, &e));
    TEST_ASSERT_EQUAL_UINT32(1, f); TEST_ASSERT_EQUAL_UINT32(1, e); /* empty log */
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_store_export_range((el_store_exp_scope_t)2, 0, 1, 1, &f, &e));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_store_export_range(EL_STORE_EXP_ALL, 0, 5, 4, &f, &e));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_store_export_range(EL_STORE_EXP_ALL, 0, 1, 1, NULL, &e));
}

static void test_export_watermark(void)
{
    TEST_ASSERT_EQUAL_UINT32(500, el_store_export_watermark(EL_STORE_EXP_ALL, 300, 501));
    TEST_ASSERT_EQUAL_UINT32(500, el_store_export_watermark(EL_STORE_EXP_SINCE_LAST, 300, 501));
    TEST_ASSERT_EQUAL_UINT32(700, el_store_export_watermark(EL_STORE_EXP_ALL, 700, 501));  /* never back */
    TEST_ASSERT_EQUAL_UINT32(7, el_store_export_watermark(EL_STORE_EXP_ALL, 7, 0));

    TEST_ASSERT_EQUAL_UINT8(0, el_store_pct(0, 400));
    TEST_ASSERT_EQUAL_UINT8(49, el_store_pct(199, 400));
    TEST_ASSERT_EQUAL_UINT8(100, el_store_pct(400, 400));
    TEST_ASSERT_EQUAL_UINT8(100, el_store_pct(0, 0));
    TEST_ASSERT_EQUAL_UINT8(99, el_store_pct(4294967294u, 4294967295u));      /* no overflow */
}

/* Realistic mirror: 10 results with the card in, card pulled for 25 results, re-inserted;
 * batches of 8 back-fill exactly the missing seqs, in order, once. */
static void test_mirror_backfill_scenario(void)
{
    el_store_mirror_t m;
    el_store_mirror_init(&m, 0, 0);
    uint32_t next = 1, from, n, on_card = 0;
    bool card = true;
    for (int i = 0; i < 60; i++) {
        if (i == 10) card = false;
        if (i == 35) card = true;
        next++;                                         /* one result logged */
        while (card && el_store_mirror_next(&m, 1, next, 8, &from, &n)) {
            TEST_ASSERT_EQUAL_UINT32(on_card + 1, from);
            for (uint32_t s = from; s < from + n; s++) { on_card = s; el_store_mirror_done(&m, s); }
        }
        TEST_ASSERT_EQUAL(!card, el_store_mirror_behind(&m, 1, next));
    }
    TEST_ASSERT_EQUAL_UINT32(60, on_card);
    TEST_ASSERT_FALSE(el_store_mirror_next(&m, 1, next, 8, &from, &n));
    TEST_ASSERT_EQUAL_UINT32(0, n);
}

static void test_mirror_edges(void)
{
    el_store_mirror_t m; uint32_t from, n;
    /* log was cleared: seqs below first are gone, skip them */
    el_store_mirror_init(&m, 40, 0);
    TEST_ASSERT_TRUE(el_store_mirror_next(&m, 100, 130, 8, &from, &n));
    TEST_ASSERT_EQUAL_UINT32(100, from); TEST_ASSERT_EQUAL_UINT32(8, n);
    /* last batch shorter */
    el_store_mirror_init(&m, 125, 0);
    TEST_ASSERT_TRUE(el_store_mirror_next(&m, 100, 130, 8, &from, &n));
    TEST_ASSERT_EQUAL_UINT32(126, from); TEST_ASSERT_EQUAL_UINT32(4, n);
    /* saved seq above the log (log reformatted, seq restarted at 1): restart */
    el_store_mirror_init(&m, 5000, 0);
    TEST_ASSERT_TRUE(el_store_mirror_behind(&m, 1, 4));
    TEST_ASSERT_TRUE(el_store_mirror_next(&m, 1, 4, 8, &from, &n));
    TEST_ASSERT_EQUAL_UINT32(1, from); TEST_ASSERT_EQUAL_UINT32(3, n);
    /* empty log */
    el_store_mirror_init(&m, 0, 0);
    TEST_ASSERT_FALSE(el_store_mirror_next(&m, 1, 1, 8, &from, &n));
    TEST_ASSERT_FALSE(el_store_mirror_behind(&m, 1, 1));
    /* done never goes backwards */
    el_store_mirror_init(&m, 10, 0);
    el_store_mirror_done(&m, 9);
    TEST_ASSERT_EQUAL_UINT32(10, m.done_seq);
    /* bad args */
    TEST_ASSERT_FALSE(el_store_mirror_next(NULL, 1, 5, 8, &from, &n));
    TEST_ASSERT_FALSE(el_store_mirror_next(&m, 1, 5, 0, &from, &n));
    TEST_ASSERT_FALSE(el_store_mirror_next(&m, 6, 5, 8, &from, &n));
}

static void test_mirror_lazy_save(void)
{
    el_store_mirror_t m;
    el_store_mirror_init(&m, 7, 1000);
    TEST_ASSERT_FALSE(el_store_mirror_save_due(&m, 900000, 30000));   /* nothing changed */
    el_store_mirror_done(&m, 8);
    TEST_ASSERT_FALSE(el_store_mirror_save_due(&m, 30999, 30000));
    TEST_ASSERT_TRUE(el_store_mirror_save_due(&m, 31000, 30000));
    el_store_mirror_saved(&m, 31000);
    TEST_ASSERT_EQUAL_UINT32(8, m.saved_seq);
    TEST_ASSERT_FALSE(el_store_mirror_save_due(&m, 99999, 30000));
    /* millisecond counter wrap */
    el_store_mirror_init(&m, 1, 0xFFFFF000u);
    el_store_mirror_done(&m, 2);
    TEST_ASSERT_TRUE(el_store_mirror_save_due(&m, 0x00010000u, 30000));
}

static void test_evline_format(void)
{
    char b[EL_STORE_EVLINE_MAX];
    size_t n = el_store_evline_format(b, sizeof b, T_REF, 123456, "boot: reason POWERON fw 0.1.0");
    TEST_ASSERT_EQUAL_STRING("2026-10-30T12:34:56Z up=123.456 boot: reason POWERON fw 0.1.0\n", b);
    TEST_ASSERT_EQUAL_UINT(strlen(b), n);

    n = el_store_evline_format(b, sizeof b, 0, 5, "no clock");
    TEST_ASSERT_EQUAL_STRING("----------T--:--:--Z up=0.005 no clock\n", b);

    el_store_evline_format(b, sizeof b, T_REF, 0, "a\nb\rc\td\x7f" "e");
    TEST_ASSERT_EQUAL_STRING("2026-10-30T12:34:56Z up=0.000 a b c d e\n", b);   /* one line, always */

    el_store_evline_format(b, sizeof b, T_REF, 0, NULL);
    TEST_ASSERT_EQUAL_STRING("2026-10-30T12:34:56Z up=0.000 \n", b);

    /* long message: cut, still one terminated line */
    char longmsg[400]; memset(longmsg, 'x', sizeof longmsg - 1); longmsg[sizeof longmsg - 1] = 0;
    n = el_store_evline_format(b, sizeof b, T_REF, 0, longmsg);
    TEST_ASSERT_EQUAL_UINT(sizeof b - 1, n);
    TEST_ASSERT_EQUAL_CHAR('\n', b[n - 1]);
    TEST_ASSERT_EQUAL_CHAR('\0', b[n]);

    n = el_store_evline_format(b, EL_STORE_EVLINE_MIN_CAP, 4102444799, 4294967295u, "hello");
    TEST_ASSERT_EQUAL_CHAR('\n', b[n - 1]);                /* worst-case prefix still fits */
    TEST_ASSERT_TRUE(n < EL_STORE_EVLINE_MIN_CAP);
    TEST_ASSERT_EQUAL_UINT(0, el_store_evline_format(b, EL_STORE_EVLINE_MIN_CAP - 1, T_REF, 0, "x"));
    TEST_ASSERT_EQUAL_UINT(0, el_store_evline_format(NULL, 100, T_REF, 0, "x"));
}

static void test_evlog_rotation(void)
{
    TEST_ASSERT_FALSE(el_store_evlog_rotate_due(0, 100, 32768));
    TEST_ASSERT_FALSE(el_store_evlog_rotate_due(32668, 100, 32768));   /* exactly at the limit */
    TEST_ASSERT_TRUE(el_store_evlog_rotate_due(32669, 100, 32768));
    TEST_ASSERT_FALSE(el_store_evlog_rotate_due(0, 40000, 32768));     /* oversize line, empty file */
    TEST_ASSERT_TRUE(el_store_evlog_rotate_due(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu));  /* no overflow */
}

void run_store_files_tests(void)
{
    RUN_TEST(test_safe_serial);
    RUN_TEST(test_export_and_mirror_names);
    RUN_TEST(test_export_range);
    RUN_TEST(test_export_watermark);
    RUN_TEST(test_mirror_backfill_scenario);
    RUN_TEST(test_mirror_edges);
    RUN_TEST(test_mirror_lazy_save);
    RUN_TEST(test_evline_format);
    RUN_TEST(test_evlog_rotation);
}
