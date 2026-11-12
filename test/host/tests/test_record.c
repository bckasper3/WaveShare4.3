#include <math.h>
#include <string.h>
#include "unity.h"
#include "el_record.h"
#include "el_crc.h"
#include "el_stats.h"
#include "el_util.h"

static el_result_t sample(void)
{
    el_result_t r = {0};
    r.seq = 0x01020304;
    r.timestamp_s = 1793361600;
    el_strlcpy(r.tag, "Server room", sizeof r.tag);
    el_strlcpy(r.operator_id, "JD", sizeof r.operator_id);
    r.temp_c = 24.6f; r.temp_sd_c = 0.03f; r.rh_pct = 52.5f; r.rh_sd_pct = 0.2f; r.repeats = 3;
    r.status = EL_RES_ALARM; r.alarms = EL_ALARM_T_HIGH;
    r.limits.t_low_c = 15.0f; r.limits.t_high_c = 24.0f;
    r.limits.rh_low_pct = 20.0f; r.limits.rh_high_pct = 60.0f;
    r.flags = EL_RES_FLAG_SIMULATED | EL_RES_FLAG_AUTO;
    return r;
}

/* Re-sign a hand-modified record so only the field under test is "wrong". */
static void resign(uint8_t *b) { el_put_u32le(b + 124, el_crc32(b, 124)); }

static void test_round_trip_all_fields(void)
{
    uint8_t b[EL_RECORD_SIZE];
    el_result_t in = sample(), out;
    TEST_ASSERT_EQUAL(EL_OK, el_record_encode(&in, b));
    TEST_ASSERT_EQUAL(EL_OK, el_record_decode(b, &out));
    TEST_ASSERT_EQUAL_UINT32(in.seq, out.seq);
    TEST_ASSERT_EQUAL_INT64(in.timestamp_s, out.timestamp_s);
    TEST_ASSERT_EQUAL_STRING(in.tag, out.tag);
    TEST_ASSERT_EQUAL_STRING(in.operator_id, out.operator_id);
    TEST_ASSERT_EQUAL_FLOAT(in.temp_c, out.temp_c);
    TEST_ASSERT_EQUAL_FLOAT(in.temp_sd_c, out.temp_sd_c);
    TEST_ASSERT_EQUAL_FLOAT(in.rh_pct, out.rh_pct);
    TEST_ASSERT_EQUAL_FLOAT(in.rh_sd_pct, out.rh_sd_pct);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, el_dew_point_c(24.6f, 52.5f), out.dew_point_c);   /* recomputed */
    TEST_ASSERT_EQUAL(in.status, out.status);
    TEST_ASSERT_EQUAL_UINT8(in.alarms, out.alarms);
    TEST_ASSERT_EQUAL_FLOAT(in.limits.t_low_c, out.limits.t_low_c);
    TEST_ASSERT_EQUAL_FLOAT(in.limits.t_high_c, out.limits.t_high_c);
    TEST_ASSERT_EQUAL_FLOAT(in.limits.rh_low_pct, out.limits.rh_low_pct);
    TEST_ASSERT_EQUAL_FLOAT(in.limits.rh_high_pct, out.limits.rh_high_pct);
    TEST_ASSERT_EQUAL_UINT8(in.repeats, out.repeats);
    TEST_ASSERT_EQUAL_UINT8(in.flags, out.flags);
}

static void test_nan_values_and_full_length_strings(void)
{
    uint8_t b[EL_RECORD_SIZE];
    el_result_t in = sample(), out;
    in.status = EL_RES_ERROR;
    in.temp_c = NAN; in.rh_pct = NAN;
    memset(in.tag, 'X', sizeof in.tag - 1);
    in.tag[sizeof in.tag - 1] = '\0';
    TEST_ASSERT_EQUAL(EL_OK, el_record_encode(&in, b));
    TEST_ASSERT_EQUAL(EL_OK, el_record_decode(b, &out));
    TEST_ASSERT_TRUE(isnan(out.temp_c));
    TEST_ASSERT_TRUE(isnan(out.dew_point_c));
    TEST_ASSERT_EQUAL_STRING(in.tag, out.tag);
}

static void test_layout_is_frozen(void)
{
    /* If this test fails, the on-flash format changed: old logs would no longer read. */
    uint8_t b[EL_RECORD_SIZE];
    el_result_t in = sample();
    el_record_encode(&in, b);
    TEST_ASSERT_EQUAL_HEX16(EL_RECORD_MAGIC, el_get_u16le(b + 0));
    TEST_ASSERT_EQUAL_UINT8(1, b[2]);
    TEST_ASSERT_EQUAL_UINT8(EL_RES_ALARM, b[3]);
    TEST_ASSERT_EQUAL_HEX32(0x01020304, el_get_u32le(b + 4));
    TEST_ASSERT_EQUAL_INT64(1793361600, (int64_t)el_get_u64le(b + 8));
    TEST_ASSERT_EQUAL_STRING("Server room", (const char *)b + 16);
    TEST_ASSERT_EQUAL_STRING("JD", (const char *)b + 36);
    TEST_ASSERT_EQUAL_FLOAT(24.6f, el_get_f32le(b + 44));
    TEST_ASSERT_EQUAL_FLOAT(52.5f, el_get_f32le(b + 52));
    TEST_ASSERT_EQUAL_FLOAT(15.0f, el_get_f32le(b + 60));
    TEST_ASSERT_EQUAL_FLOAT(60.0f, el_get_f32le(b + 72));
    TEST_ASSERT_EQUAL_UINT8(3, b[76]);
    TEST_ASSERT_EQUAL_UINT8(EL_RES_FLAG_SIMULATED | EL_RES_FLAG_AUTO, b[77]);
    TEST_ASSERT_EQUAL_UINT8(EL_ALARM_T_HIGH, b[78]);
    for (int i = 79; i < 124; i++) TEST_ASSERT_EQUAL_UINT8(0, b[i]);
    TEST_ASSERT_EQUAL_HEX32(el_crc32(b, 124), el_get_u32le(b + 124));
}

static void test_erased_and_blank_flash_is_not_a_record(void)
{
    uint8_t b[EL_RECORD_SIZE];
    el_result_t out;
    memset(b, 0xFF, sizeof b);
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_record_decode(b, &out));
    memset(b, 0x00, sizeof b);
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_record_decode(b, &out));
}

static void test_every_single_bit_flip_is_detected(void)
{
    uint8_t good[EL_RECORD_SIZE], b[EL_RECORD_SIZE];
    el_result_t in = sample(), out;
    el_record_encode(&in, good);
    for (int bit = 0; bit < EL_RECORD_SIZE * 8; bit++) {
        memcpy(b, good, sizeof b);
        b[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        TEST_ASSERT_NOT_EQUAL(EL_OK, el_record_decode(b, &out));
    }
}

static void test_torn_write_is_detected(void)
{
    /* Power cut while writing over erased flash: first k bytes new, rest still 0xFF. */
    uint8_t good[EL_RECORD_SIZE], b[EL_RECORD_SIZE];
    el_result_t in = sample(), out;
    el_record_encode(&in, good);
    for (int k = 0; k < EL_RECORD_SIZE; k++) {
        memset(b, 0xFF, sizeof b);
        memcpy(b, good, (size_t)k);
        TEST_ASSERT_NOT_EQUAL(EL_OK, el_record_decode(b, &out));
    }
}

static void test_newer_version_is_reported(void)
{
    uint8_t b[EL_RECORD_SIZE];
    el_result_t in = sample(), out;
    el_record_encode(&in, b);
    b[2] = EL_RECORD_VERSION + 1;
    resign(b);
    TEST_ASSERT_EQUAL(EL_ERR_VERSION, el_record_decode(b, &out));
}

static void test_bad_status_is_rejected(void)
{
    uint8_t b[EL_RECORD_SIZE];
    el_result_t in = sample(), out;
    el_record_encode(&in, b);
    b[3] = EL_RES_STATUS_COUNT;
    resign(b);
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_record_decode(b, &out));
    in.status = (el_result_status_t)99;
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_record_encode(&in, b));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_record_encode(NULL, b));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_record_decode(NULL, &out));
}

void run_record_tests(void)
{
    RUN_TEST(test_round_trip_all_fields);
    RUN_TEST(test_nan_values_and_full_length_strings);
    RUN_TEST(test_layout_is_frozen);
    RUN_TEST(test_erased_and_blank_flash_is_not_a_record);
    RUN_TEST(test_every_single_bit_flip_is_detected);
    RUN_TEST(test_torn_write_is_detected);
    RUN_TEST(test_newer_version_is_reported);
    RUN_TEST(test_bad_status_is_rejected);
}
