#include <math.h>
#include <string.h>
#include "unity.h"
#include "el_modbus.h"
#include "el_util.h"

#define ADDR 17

static el_mb_t g;

static void fresh(void)
{
    el_mb_inputs_t in;
    memset(&in, 0, sizeof in);
    in.temp_c = 23.5f; in.status = 2; in.result_counter = 7; in.last_seq = 0x00012345u;
    in.last_timestamp_s = 1793361600u; in.alarms = EL_ALARM_RH_HIGH; in.repeats = 3;
    in.rh_pct = 61.25f; in.dew_point_c = 15.5f; in.temp_sd_c = 0.25f; in.rh_sd_pct = 0.5f;
    in.meas_state = 2; in.progress_pct = 100;
    in.flags = EL_MB_FLAG_SIMULATED | EL_MB_FLAG_AUTOLOG_ON;
    in.next_auto_s = 287;
    in.fw_major = 1; in.fw_minor = 2; in.fw_patch = 3;
    el_strlcpy(in.serial, "EL2610-0001", sizeof in.serial);
    el_strlcpy(in.last_tag, "Cold store 1", sizeof in.last_tag);
    in.uptime_s = 86400u * 3; in.log_count = 1234; in.log_used_pct = 12;
    TEST_ASSERT_EQUAL(EL_OK, el_mb_init(&g, ADDR));
    el_mb_set_inputs(&g, &in);
}

/* Talk to the slave like a real master: build, process, parse. */
static el_err_t exchange(const uint8_t *req, size_t n, uint8_t fc, el_mb_master_resp_t *r, size_t *rlen)
{
    uint8_t resp[EL_MB_RTU_MAX_FRAME];
    size_t l = el_mb_rtu_process(&g, req, n, resp, sizeof resp);
    if (rlen) *rlen = l;
    if (l == 0) return EL_ERR_SIZE;
    return el_mb_master_parse(resp, l, req[0], fc, r);
}

static el_err_t read_regs(uint8_t fc, uint16_t start, uint16_t qty, el_mb_master_resp_t *r)
{
    uint8_t req[8];
    size_t n = el_mb_master_read(req, sizeof req, ADDR, fc, start, qty);
    TEST_ASSERT_EQUAL(8, n);
    return exchange(req, n, fc, r, NULL);
}

static void expect_read_ok(uint8_t fc, uint16_t start, uint16_t qty, el_mb_master_resp_t *r)
{
    TEST_ASSERT_EQUAL(EL_OK, read_regs(fc, start, qty, r));
    TEST_ASSERT_EQUAL_UINT8(0, r->exception);
    TEST_ASSERT_EQUAL_UINT16(qty, r->quantity);
}

static void expect_read_ex(uint8_t fc, uint16_t start, uint16_t qty, uint8_t ex)
{
    el_mb_master_resp_t r;
    TEST_ASSERT_EQUAL(EL_OK, read_regs(fc, start, qty, &r));
    TEST_ASSERT_EQUAL_UINT8(ex, r.exception);
}

static uint8_t write1(uint16_t reg, uint16_t v)
{
    uint8_t req[8];
    el_mb_master_resp_t r;
    size_t n = el_mb_master_write_single(req, sizeof req, ADDR, reg, v);
    TEST_ASSERT_EQUAL(EL_OK, exchange(req, n, EL_MB_FC_WRITE_SINGLE, &r, NULL));
    if (!r.exception) {
        TEST_ASSERT_EQUAL_UINT16(reg, r.start);
        TEST_ASSERT_EQUAL_UINT16(v, r.regs[0]);
    }
    return r.exception;
}

static uint8_t writeN(uint16_t start, const uint16_t *v, uint16_t qty)
{
    uint8_t req[260];
    el_mb_master_resp_t r;
    size_t n = el_mb_master_write_multi(req, sizeof req, ADDR, start, v, qty);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL(EL_OK, exchange(req, n, EL_MB_FC_WRITE_MULTI, &r, NULL));
    if (!r.exception) {
        TEST_ASSERT_EQUAL_UINT16(start, r.start);
        TEST_ASSERT_EQUAL_UINT16(qty, r.quantity);
    }
    return r.exception;
}

/* ---------- CRC and framing against known frames ---------- */

static void test_crc_known_frames(void)
{
    const uint8_t a[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    TEST_ASSERT_EQUAL_HEX16(0xCDC5, el_mb_crc16(a, sizeof a));   /* wire bytes C5 CD (low first) */
    const uint8_t b[] = {0x11, 0x03, 0x00, 0x6B, 0x00, 0x03};    /* Modbus spec example */
    TEST_ASSERT_EQUAL_HEX16(0x8776, el_mb_crc16(b, sizeof b));
    uint8_t buf[8];
    memcpy(buf, a, 6);
    TEST_ASSERT_EQUAL(8, el_mb_frame_seal(buf, 6));
    TEST_ASSERT_EQUAL_HEX8(0xC5, buf[6]);
    TEST_ASSERT_EQUAL_HEX8(0xCD, buf[7]);
}

static void test_master_builders_produce_known_bytes(void)
{
    uint8_t b[16];
    TEST_ASSERT_EQUAL(8, el_mb_master_read(b, sizeof b, 1, 3, 0, 10));
    const uint8_t want[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, b, 8);
    TEST_ASSERT_EQUAL(0, el_mb_master_read(b, 7, 1, 3, 0, 10));        /* buffer too small */
    TEST_ASSERT_EQUAL(0, el_mb_master_read(b, sizeof b, 1, 3, 0, 0));  /* qty 0 */
    uint16_t v[2] = {1, 2};
    TEST_ASSERT_EQUAL(13, el_mb_master_write_multi(b, sizeof b, 1, 100, v, 2));
    TEST_ASSERT_EQUAL(0, el_mb_master_write_multi(b, 12, 1, 100, v, 2));
}

/* ---------- reading ---------- */

static void test_read_input_registers_full_map(void)
{
    fresh();
    el_mb_master_resp_t r;
    expect_read_ok(EL_MB_FC_READ_INPUT, EL_MB_IR_TEMP, 2, &r);
    TEST_ASSERT_EQUAL_FLOAT(23.5f, el_mb_get_f32(r.regs));
    /* big-endian word order: 23.5f = 0x41BC0000, high word first */
    TEST_ASSERT_EQUAL_HEX16(0x41BC, r.regs[0]);
    TEST_ASSERT_EQUAL_HEX16(0x0000, r.regs[1]);

    expect_read_ok(EL_MB_FC_READ_INPUT, 2, 2, &r);              /* status, counter */
    TEST_ASSERT_EQUAL_UINT16(2, r.regs[0]);
    TEST_ASSERT_EQUAL_UINT16(7, r.regs[1]);
    expect_read_ok(EL_MB_FC_READ_INPUT, EL_MB_IR_TIMESTAMP, 2, &r);
    TEST_ASSERT_EQUAL_UINT32(1793361600u, el_mb_get_u32(r.regs));
    expect_read_ok(EL_MB_FC_READ_INPUT, EL_MB_IR_MEAS_STATE, 4, &r);   /* state, alarms, progress, flags */
    TEST_ASSERT_EQUAL_UINT16(2, r.regs[0]);
    TEST_ASSERT_EQUAL_UINT16(EL_ALARM_RH_HIGH, r.regs[1]);
    TEST_ASSERT_EQUAL_UINT16(100, r.regs[2]);
    TEST_ASSERT_EQUAL_UINT16(EL_MB_FLAG_SIMULATED | EL_MB_FLAG_AUTOLOG_ON, r.regs[3]);
    expect_read_ok(EL_MB_FC_READ_INPUT, EL_MB_IR_HUMIDITY, 5, &r);     /* RH(2), repeats, dew(2) */
    TEST_ASSERT_EQUAL_FLOAT(61.25f, el_mb_get_f32(r.regs));
    TEST_ASSERT_EQUAL_UINT16(3, r.regs[2]);
    TEST_ASSERT_EQUAL_FLOAT(15.5f, el_mb_get_f32(r.regs + 3));
    expect_read_ok(EL_MB_FC_READ_INPUT, EL_MB_IR_TEMP_SD, 6, &r);      /* temp SD, seq, RH SD */
    TEST_ASSERT_EQUAL_FLOAT(0.25f, el_mb_get_f32(r.regs));
    TEST_ASSERT_EQUAL_UINT32(0x12345u, el_mb_get_u32(r.regs + 2));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, el_mb_get_f32(r.regs + 4));
    expect_read_ok(EL_MB_FC_READ_INPUT, EL_MB_IR_UPTIME, 7, &r);       /* uptime, count, pct, next */
    TEST_ASSERT_EQUAL_UINT32(259200u, el_mb_get_u32(r.regs));
    TEST_ASSERT_EQUAL_UINT32(1234u, el_mb_get_u32(r.regs + 2));
    TEST_ASSERT_EQUAL_UINT16(12, r.regs[4]);
    TEST_ASSERT_EQUAL_UINT32(287u, el_mb_get_u32(r.regs + 5));
    expect_read_ok(EL_MB_FC_READ_INPUT, 0, 28, &r);                    /* the whole main block at once */
    expect_read_ok(EL_MB_FC_READ_INPUT, EL_MB_IR_FW_MAJOR, 3, &r);
    TEST_ASSERT_EQUAL_UINT16(1, r.regs[0]);
    TEST_ASSERT_EQUAL_UINT16(2, r.regs[1]);
    TEST_ASSERT_EQUAL_UINT16(3, r.regs[2]);
}

static void test_float_nan_round_trip(void)
{
    fresh();
    el_mb_inputs_t in = g.inputs;
    in.dew_point_c = NAN;
    el_mb_set_inputs(&g, &in);
    el_mb_master_resp_t r;
    expect_read_ok(EL_MB_FC_READ_INPUT, EL_MB_IR_DEW_POINT, 2, &r);
    TEST_ASSERT_TRUE(isnan(el_mb_get_f32(r.regs)));
    float vals[] = {0.0f, -0.0f, 1.0e-30f, 3.4e38f, -250.75f, INFINITY};
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        uint16_t w[2];
        el_mb_put_f32(w, vals[i]);
        TEST_ASSERT_EQUAL_FLOAT(vals[i], el_mb_get_f32(w));
    }
}

static void test_string_registers(void)
{
    fresh();
    el_mb_master_resp_t r;
    char s[32];
    expect_read_ok(EL_MB_FC_READ_INPUT, EL_MB_IR_SERIAL, EL_MB_SERIAL_REGS, &r);
    TEST_ASSERT_EQUAL(EL_OK, el_mb_get_str(r.regs, EL_MB_SERIAL_REGS, s, sizeof s));
    TEST_ASSERT_EQUAL_STRING("EL2610-0001", s);

    expect_read_ok(EL_MB_FC_READ_INPUT, EL_MB_IR_TAG, EL_MB_TAG_REGS, &r);
    TEST_ASSERT_EQUAL_HEX16(('C' << 8) | 'o', r.regs[0]);        /* first char in the high byte */
    el_mb_get_str(r.regs, EL_MB_TAG_REGS, s, sizeof s);
    TEST_ASSERT_EQUAL_STRING("Cold store 1", s);

    /* odd length: last register holds one char + NUL */
    el_mb_inputs_t in = g.inputs;
    el_strlcpy(in.last_tag, "ABC", sizeof in.last_tag);
    el_mb_set_inputs(&g, &in);
    expect_read_ok(EL_MB_FC_READ_INPUT, EL_MB_IR_TAG, EL_MB_TAG_REGS, &r);
    TEST_ASSERT_EQUAL_HEX16('C' << 8, r.regs[1]);
    TEST_ASSERT_EQUAL_HEX16(0, r.regs[2]);

    /* empty */
    in.last_tag[0] = '\0';
    el_mb_set_inputs(&g, &in);
    expect_read_ok(EL_MB_FC_READ_INPUT, EL_MB_IR_TAG, EL_MB_TAG_REGS, &r);
    for (int i = 0; i < EL_MB_TAG_REGS; i++) TEST_ASSERT_EQUAL_HEX16(0, r.regs[i]);

    /* full: 20 chars fill all 10 registers, no terminator on the wire */
    memset(in.last_tag, 'Z', 20);
    in.last_tag[20] = '\0';
    el_mb_set_inputs(&g, &in);
    expect_read_ok(EL_MB_FC_READ_INPUT, EL_MB_IR_TAG, EL_MB_TAG_REGS, &r);
    TEST_ASSERT_EQUAL_HEX16(('Z' << 8) | 'Z', r.regs[9]);
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_mb_get_str(r.regs, EL_MB_TAG_REGS, s, 20));  /* 20 chars need cap 21 */
    TEST_ASSERT_EQUAL_UINT8('\0', s[19]);
    TEST_ASSERT_EQUAL(EL_OK, el_mb_get_str(r.regs, EL_MB_TAG_REGS, s, 21));
    TEST_ASSERT_EQUAL(20, strlen(s));

    /* firmware gave an unterminated buffer: engine must still stay inside it */
    memset(in.serial, 'Q', sizeof in.serial);
    el_mb_set_inputs(&g, &in);
    expect_read_ok(EL_MB_FC_READ_INPUT, EL_MB_IR_SERIAL, EL_MB_SERIAL_REGS, &r);
    TEST_ASSERT_EQUAL_HEX16(('Q' << 8) | 'Q', r.regs[7]);
}

static void test_get_str_rejects_bad_data(void)
{
    uint16_t w[2];
    char s[8];
    w[0] = 0x4100; w[1] = 0x4200;                       /* 'A' NUL 'B' NUL: data after NUL */
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_mb_get_str(w, 2, s, sizeof s));
    w[0] = 0x4101; w[1] = 0;                            /* control char */
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_mb_get_str(w, 2, s, sizeof s));
    w[0] = 0x41FF; w[1] = 0;                            /* non-ASCII */
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_mb_get_str(w, 2, s, sizeof s));
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_mb_get_str((uint16_t[]){0x4142, 0x4344}, 2, s, 3));
    TEST_ASSERT_EQUAL_STRING("AB", s);
}

static void test_read_holding_defaults_and_command_reads_zero(void)
{
    fresh();
    el_mb_master_resp_t r;
    expect_read_ok(EL_MB_FC_READ_HOLDING, EL_MB_HR_COMMAND, 10, &r);
    TEST_ASSERT_EQUAL_UINT16(0, r.regs[0]);
    TEST_ASSERT_EQUAL_UINT16(3, r.regs[1]);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, el_mb_get_f32(r.regs + 2));
    TEST_ASSERT_EQUAL_FLOAT(35.0f, el_mb_get_f32(r.regs + 4));
    TEST_ASSERT_EQUAL_FLOAT(20.0f, el_mb_get_f32(r.regs + 6));
    TEST_ASSERT_EQUAL_FLOAT(80.0f, el_mb_get_f32(r.regs + 8));
    expect_read_ok(EL_MB_FC_READ_HOLDING, EL_MB_HR_ADDRESS, 5, &r);
    TEST_ASSERT_EQUAL_UINT16(ADDR, r.regs[0]);
    TEST_ASSERT_EQUAL_UINT16(1, r.regs[1]);
    TEST_ASSERT_EQUAL_UINT16(1, r.regs[2]);
    TEST_ASSERT_EQUAL_UINT32(300, el_mb_get_u32(r.regs + 3));
    expect_read_ok(EL_MB_FC_READ_HOLDING, EL_MB_HR_COMMAND, 15, &r);   /* the whole block */
    /* a command, once written, still reads 0 */
    TEST_ASSERT_EQUAL_UINT8(0, write1(EL_MB_HR_COMMAND, EL_MB_CMD_START));
    expect_read_ok(EL_MB_FC_READ_HOLDING, EL_MB_HR_COMMAND, 1, &r);
    TEST_ASSERT_EQUAL_UINT16(0, r.regs[0]);
}

static void test_maximum_read_quantity(void)
{
    fresh();
    el_mb_master_resp_t r;
    expect_read_ok(EL_MB_FC_READ_INPUT, 0, 2, &r);
    /* 125 registers is legal as a count but this range has gaps -> 02, not a crash */
    expect_read_ex(EL_MB_FC_READ_INPUT, 0, 125, EL_MB_EX_ADDRESS);
}

/* ---------- exceptions ---------- */

static void test_exception_illegal_function(void)
{
    fresh();
    const uint8_t fcs[] = {0x01, 0x02, 0x05, 0x0F, 0x17, 0x2A, 0x64, 0x7F};
    for (size_t i = 0; i < sizeof fcs; i++) {
        uint8_t req[8] = {ADDR, fcs[i], 0, 0, 0, 1};
        size_t n = el_mb_frame_seal(req, 6);
        el_mb_master_resp_t r;
        TEST_ASSERT_EQUAL(EL_OK, exchange(req, n, fcs[i], &r, NULL));
        TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_FUNCTION, r.exception);
    }
}

static void test_exception_address_and_gaps(void)
{
    fresh();
    expect_read_ex(EL_MB_FC_READ_INPUT, 100, 1, EL_MB_EX_ADDRESS);   /* holding addr on input table */
    expect_read_ex(EL_MB_FC_READ_INPUT, 18, 1, EL_MB_EX_ADDRESS);    /* inside u32 (seq hi is 17) */
    expect_read_ex(EL_MB_FC_READ_INPUT, 26, 3, EL_MB_EX_ADDRESS);    /* next-auto .. gap at 28 */
    expect_read_ex(EL_MB_FC_READ_INPUT, 12, 2, EL_MB_EX_ADDRESS);    /* ends inside the dew point */
    expect_read_ex(EL_MB_FC_READ_INPUT, 1, 1, EL_MB_EX_ADDRESS);     /* second half of temperature */
    expect_read_ex(EL_MB_FC_READ_INPUT, 0, 1, EL_MB_EX_ADDRESS);     /* first half of temperature */
    expect_read_ex(EL_MB_FC_READ_INPUT, 33, 1, EL_MB_EX_ADDRESS);    /* gap after the version */
    expect_read_ex(EL_MB_FC_READ_INPUT, EL_MB_IR_TAG, 5, EL_MB_EX_ADDRESS);   /* half a string */
    expect_read_ex(EL_MB_FC_READ_HOLDING, 103, 1, EL_MB_EX_ADDRESS);
    expect_read_ex(EL_MB_FC_READ_HOLDING, 0, 1, EL_MB_EX_ADDRESS);
    expect_read_ex(EL_MB_FC_READ_HOLDING, 114, 1, EL_MB_EX_ADDRESS); /* second half of the interval */
    expect_read_ex(EL_MB_FC_READ_HOLDING, 113, 3, EL_MB_EX_ADDRESS); /* 115 unmapped */
    /* partial writes of 32-bit items */
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_ADDRESS, write1(EL_MB_HR_T_LOW, 0));
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_ADDRESS, write1(EL_MB_HR_T_LOW + 1, 0));
    uint16_t v[3] = {0x40A0, 0, 0};
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_ADDRESS, writeN(EL_MB_HR_T_LOW, v, 3));  /* 3 regs: tears T high */
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_ADDRESS, write1(EL_MB_HR_LOG_INTERVAL, 60));
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_ADDRESS, write1(0, 1));                 /* read-only/unmapped */
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_ADDRESS, write1(EL_MB_IR_ALARMS, 1));   /* input reg not writable */
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_ADDRESS, write1(115, 1));
}

static void test_address_range_end(void)
{
    fresh();
    expect_read_ex(EL_MB_FC_READ_HOLDING, 65535, 1, EL_MB_EX_ADDRESS);   /* in range, unmapped */
    expect_read_ex(EL_MB_FC_READ_HOLDING, 65535, 2, EL_MB_EX_ADDRESS);   /* overflows 65536 */
    expect_read_ex(EL_MB_FC_READ_INPUT, 65530, 125, EL_MB_EX_ADDRESS);
    uint16_t v[2] = {0, 0};
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_ADDRESS, writeN(65535, v, 2));
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_ADDRESS, writeN(65535, v, 1));
}

static void test_exception_value_quantities(void)
{
    fresh();
    el_mb_master_resp_t r;
    uint8_t req[300];
    /* read qty 0 and 126 must be 03 (builder refuses, so hand-build) */
    uint16_t qtys[] = {0, 126, 0xFFFF};
    for (size_t i = 0; i < 3; i++) {
        req[0] = ADDR; req[1] = EL_MB_FC_READ_INPUT; req[2] = 0; req[3] = 0;
        req[4] = (uint8_t)(qtys[i] >> 8); req[5] = (uint8_t)qtys[i];
        size_t n = el_mb_frame_seal(req, 6);
        TEST_ASSERT_EQUAL(EL_OK, exchange(req, n, EL_MB_FC_READ_INPUT, &r, NULL));
        TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, r.exception);
    }
    /* write qty 0 and 124 */
    uint16_t big[124] = {0};
    TEST_ASSERT_EQUAL(0, el_mb_master_write_multi(req, sizeof req, ADDR, 100, big, 124));
    req[0] = ADDR; req[1] = EL_MB_FC_WRITE_MULTI; req[2] = 0; req[3] = 100; req[4] = 0; req[5] = 124;
    req[6] = 248;
    memset(req + 7, 0, 248);
    size_t n = el_mb_frame_seal(req, 255);             /* 257 bytes: over the RTU limit, dropped */
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, exchange(req, n, EL_MB_FC_WRITE_MULTI, &r, NULL));
    n = el_mb_frame_seal(req, 11);                      /* qty 124 with a short body */
    TEST_ASSERT_EQUAL(EL_OK, exchange(req, n, EL_MB_FC_WRITE_MULTI, &r, NULL));
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, r.exception);
    req[4] = 0; req[5] = 0; req[6] = 0;
    n = el_mb_frame_seal(req, 7);
    TEST_ASSERT_EQUAL(EL_OK, exchange(req, n, EL_MB_FC_WRITE_MULTI, &r, NULL));
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, r.exception);
    /* byte count disagrees with quantity */
    uint16_t v[2] = {0x40A0, 0};
    n = el_mb_master_write_multi(req, sizeof req, ADDR, EL_MB_HR_T_LOW, v, 2);
    req[6] = 6;
    n = el_mb_frame_seal(req, n - 2);
    TEST_ASSERT_EQUAL(EL_OK, exchange(req, n, EL_MB_FC_WRITE_MULTI, &r, NULL));
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, r.exception);
    /* frame longer than byte count says */
    n = el_mb_master_write_multi(req, sizeof req, ADDR, EL_MB_HR_T_LOW, v, 2);
    req[n - 2] = 0x55;
    n = el_mb_frame_seal(req, n - 1);
    TEST_ASSERT_EQUAL(EL_OK, exchange(req, n, EL_MB_FC_WRITE_MULTI, &r, NULL));
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, r.exception);
    /* truncated read request (3 PDU bytes) and truncated FC06 */
    uint8_t shortreq[6] = {ADDR, EL_MB_FC_READ_HOLDING, 0, 0};
    n = el_mb_frame_seal(shortreq, 4);
    TEST_ASSERT_EQUAL(EL_OK, exchange(shortreq, n, EL_MB_FC_READ_HOLDING, &r, NULL));
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, r.exception);
    TEST_ASSERT_EQUAL(0, g.holding.repeats == 3 ? 0 : 1);
}

static void test_max_write_quantity_is_accepted_in_framing(void)
{
    fresh();
    /* 123 registers from 100 is legal framing but unmapped past 114: 02 not 03 */
    uint16_t v[123] = {0};
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_ADDRESS, writeN(100, v, 123));
}

static void test_value_validation_each_setting(void)
{
    fresh();
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, write1(EL_MB_HR_COMMAND, 4));
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, write1(EL_MB_HR_COMMAND, 0xFFFF));
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, write1(EL_MB_HR_REPEATS, 0));
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, write1(EL_MB_HR_REPEATS, 11));
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, write1(EL_MB_HR_ADDRESS, 0));
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, write1(EL_MB_HR_ADDRESS, 248));
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, write1(EL_MB_HR_BAUD, 5));
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, write1(EL_MB_HR_PARITY, 3));
    float bad_t[] = {NAN, INFINITY, -INFINITY, -40.5f, 125.5f};
    for (size_t i = 0; i < sizeof bad_t / sizeof bad_t[0]; i++) {
        uint16_t w[2];
        el_mb_put_f32(w, bad_t[i]);
        TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, writeN(EL_MB_HR_T_LOW, w, 2));
        TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, writeN(EL_MB_HR_T_HIGH, w, 2));
    }
    float bad_h[] = {NAN, -0.5f, 100.5f};
    for (size_t i = 0; i < sizeof bad_h / sizeof bad_h[0]; i++) {
        uint16_t w[2];
        el_mb_put_f32(w, bad_h[i]);
        TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, writeN(EL_MB_HR_RH_LOW, w, 2));
        TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, writeN(EL_MB_HR_RH_HIGH, w, 2));
    }
    uint16_t same[2];                                          /* low == high: refused */
    el_mb_put_f32(same, g.holding.limits.t_high_c);
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, writeN(EL_MB_HR_T_LOW, same, 2));
    el_mb_put_f32(same, g.holding.limits.rh_low_pct);
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, writeN(EL_MB_HR_RH_HIGH, same, 2));
    uint16_t iv[2];
    el_mb_put_u32(iv, 9);                                      /* interval: 0 or 10..86400 */
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, writeN(EL_MB_HR_LOG_INTERVAL, iv, 2));
    el_mb_put_u32(iv, 86401);
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, writeN(EL_MB_HR_LOG_INTERVAL, iv, 2));
    el_mb_effects_t fx;
    TEST_ASSERT_FALSE(el_mb_take_effects(&g, &fx));            /* nothing slipped through */
    TEST_ASSERT_EQUAL_FLOAT(5.0f, g.holding.limits.t_low_c);
    TEST_ASSERT_EQUAL_UINT32(300, g.holding.log_interval_s);

    /* range ends (= limit effectively off) are accepted */
    uint16_t w[4];
    el_mb_put_f32(w, -40.0f);
    el_mb_put_f32(w + 2, 125.0f);
    TEST_ASSERT_EQUAL_UINT8(0, writeN(EL_MB_HR_T_LOW, w, 4));
    el_mb_put_f32(w, 0.0f);
    el_mb_put_f32(w + 2, 100.0f);
    TEST_ASSERT_EQUAL_UINT8(0, writeN(EL_MB_HR_RH_LOW, w, 4));
}

/* ---------- writing ---------- */

static void test_write_each_setting_reports_effect(void)
{
    fresh();
    el_mb_effects_t fx;
    TEST_ASSERT_EQUAL_UINT8(0, write1(EL_MB_HR_REPEATS, 5));
    TEST_ASSERT_EQUAL_UINT8(0, write1(EL_MB_HR_ADDRESS, 42));
    TEST_ASSERT_EQUAL_UINT8(0, write1(EL_MB_HR_BAUD, 4));
    TEST_ASSERT_EQUAL_UINT8(0, write1(EL_MB_HR_PARITY, 0));
    TEST_ASSERT_TRUE(el_mb_take_effects(&g, &fx));
    TEST_ASSERT_EQUAL_HEX32(EL_MB_FX_REPEATS | EL_MB_FX_ADDRESS | EL_MB_FX_BAUD | EL_MB_FX_PARITY, fx.mask);
    TEST_ASSERT_EQUAL_UINT16(5, fx.set.repeats);
    TEST_ASSERT_EQUAL_UINT16(42, fx.set.address);
    TEST_ASSERT_EQUAL_UINT16(4, fx.set.baud_code);
    TEST_ASSERT_EQUAL_UINT16(0, fx.set.parity_code);
    TEST_ASSERT_FALSE(el_mb_take_effects(&g, &fx));            /* cleared by take */
    TEST_ASSERT_EQUAL_UINT8(ADDR, g.address);                   /* active address unchanged until reboot */

    el_mb_master_resp_t r;
    expect_read_ok(EL_MB_FC_READ_HOLDING, EL_MB_HR_REPEATS, 1, &r);   /* read-back shows the write */
    TEST_ASSERT_EQUAL_UINT16(5, r.regs[0]);
}

static void test_commands_are_reported_not_stored(void)
{
    fresh();
    el_mb_effects_t fx;
    for (uint16_t c = 1; c <= 3; c++) {
        TEST_ASSERT_EQUAL_UINT8(0, write1(EL_MB_HR_COMMAND, c));
        TEST_ASSERT_TRUE(el_mb_take_effects(&g, &fx));
        TEST_ASSERT_EQUAL_HEX32(EL_MB_FX_COMMAND, fx.mask);
        TEST_ASSERT_EQUAL(c, fx.command);
    }
    TEST_ASSERT_EQUAL_UINT8(0, write1(EL_MB_HR_COMMAND, 0));         /* no-op, accepted */
    TEST_ASSERT_FALSE(el_mb_take_effects(&g, &fx));
    write1(EL_MB_HR_COMMAND, 1);
    write1(EL_MB_HR_COMMAND, 2);                                     /* latest wins */
    el_mb_take_effects(&g, &fx);
    TEST_ASSERT_EQUAL(EL_MB_CMD_CANCEL, fx.command);
}

static void test_write_limits_float_pair(void)
{
    fresh();
    uint16_t w[4];
    el_mb_put_f32(w, 2.5f);                                    /* vaccine fridge 2.5..8 degC */
    el_mb_put_f32(w + 2, 8.0f);
    TEST_ASSERT_EQUAL_UINT8(0, writeN(EL_MB_HR_T_LOW, w, 4));
    el_mb_master_resp_t r;
    expect_read_ok(EL_MB_FC_READ_HOLDING, EL_MB_HR_T_LOW, 4, &r);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, el_mb_get_f32(r.regs));
    TEST_ASSERT_EQUAL_FLOAT(8.0f, el_mb_get_f32(r.regs + 2));
    el_mb_effects_t fx;
    TEST_ASSERT_TRUE(el_mb_take_effects(&g, &fx));
    TEST_ASSERT_EQUAL_HEX32(EL_MB_FX_T_LOW | EL_MB_FX_T_HIGH, fx.mask);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, fx.set.limits.t_low_c);

    /* low above the stored high is refused, below is fine */
    el_mb_put_f32(w, 9.0f);
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, writeN(EL_MB_HR_T_LOW, w, 2));
    el_mb_put_f32(w, 3.0f);
    TEST_ASSERT_EQUAL_UINT8(0, writeN(EL_MB_HR_T_LOW, w, 2));
    /* high below stored low refused */
    el_mb_put_f32(w, 2.0f);
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, writeN(EL_MB_HR_T_HIGH, w, 2));
    /* reversing both in one request is judged on the new pair */
    el_mb_put_f32(w, 50.0f);
    el_mb_put_f32(w + 2, 40.0f);
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, writeN(EL_MB_HR_T_LOW, w, 4));
    el_mb_put_f32(w, 40.0f);
    el_mb_put_f32(w + 2, 50.0f);                               /* moving the band above the old one */
    TEST_ASSERT_EQUAL_UINT8(0, writeN(EL_MB_HR_T_LOW, w, 4));
    /* humidity pair, and all four limits in one request */
    uint16_t all[8];
    el_mb_put_f32(all, 15.0f); el_mb_put_f32(all + 2, 25.0f);
    el_mb_put_f32(all + 4, 40.0f); el_mb_put_f32(all + 6, 55.0f);    /* archive room */
    TEST_ASSERT_EQUAL_UINT8(0, writeN(EL_MB_HR_T_LOW, all, 8));
    el_mb_take_effects(&g, &fx);
    TEST_ASSERT_EQUAL_HEX32(EL_MB_FX_T_LOW | EL_MB_FX_T_HIGH | EL_MB_FX_RH_LOW | EL_MB_FX_RH_HIGH, fx.mask);
    TEST_ASSERT_EQUAL_FLOAT(55.0f, fx.set.limits.rh_high_pct);
}

static void test_write_log_interval(void)
{
    fresh();
    uint16_t w[2];
    el_mb_effects_t fx;
    el_mb_put_u32(w, 600);
    TEST_ASSERT_EQUAL_UINT8(0, writeN(EL_MB_HR_LOG_INTERVAL, w, 2));
    TEST_ASSERT_TRUE(el_mb_take_effects(&g, &fx));
    TEST_ASSERT_EQUAL_HEX32(EL_MB_FX_INTERVAL, fx.mask);
    TEST_ASSERT_EQUAL_UINT32(600, fx.set.log_interval_s);
    el_mb_master_resp_t r;
    expect_read_ok(EL_MB_FC_READ_HOLDING, EL_MB_HR_LOG_INTERVAL, 2, &r);
    TEST_ASSERT_EQUAL_UINT32(600, el_mb_get_u32(r.regs));
    el_mb_put_u32(w, 0);                                       /* off */
    TEST_ASSERT_EQUAL_UINT8(0, writeN(EL_MB_HR_LOG_INTERVAL, w, 2));
    el_mb_put_u32(w, 86400);                                   /* once a day */
    TEST_ASSERT_EQUAL_UINT8(0, writeN(EL_MB_HR_LOG_INTERVAL, w, 2));
    el_mb_take_effects(&g, &fx);
    TEST_ASSERT_EQUAL_UINT32(86400, fx.set.log_interval_s);
    el_mb_put_u32(w, 0x00010000u | 60u);                       /* high word set: 65596 s, valid */
    TEST_ASSERT_EQUAL_UINT8(0, writeN(EL_MB_HR_LOG_INTERVAL, w, 2));
    el_mb_put_u32(w, 0xFFFFFFFFu);
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, writeN(EL_MB_HR_LOG_INTERVAL, w, 2));
}

static void test_multi_write_is_atomic(void)
{
    fresh();
    el_mb_effects_t fx;
    /* address OK, baud invalid: neither applied */
    uint16_t v[3] = {55, 9, 1};
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, writeN(EL_MB_HR_ADDRESS, v, 3));
    TEST_ASSERT_EQUAL_UINT16(ADDR, g.holding.address);
    TEST_ASSERT_FALSE(el_mb_take_effects(&g, &fx));
    /* start command + invalid repeats: the command must not leak out */
    uint16_t c[2] = {EL_MB_CMD_START, 99};
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, writeN(EL_MB_HR_COMMAND, c, 2));
    TEST_ASSERT_FALSE(el_mb_take_effects(&g, &fx));
    TEST_ASSERT_EQUAL_UINT16(3, g.holding.repeats);
    /* valid low limit, invalid high limit in one request: nothing applied */
    uint16_t lim[4];
    el_mb_put_f32(lim, 2.0f);
    el_mb_put_f32(lim + 2, NAN);
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_VALUE, writeN(EL_MB_HR_T_LOW, lim, 4));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, g.holding.limits.t_low_c);
    /* the valid version of the same block applies fully */
    uint16_t ok[3] = {55, 2, 2};
    TEST_ASSERT_EQUAL_UINT8(0, writeN(EL_MB_HR_ADDRESS, ok, 3));
    TEST_ASSERT_TRUE(el_mb_take_effects(&g, &fx));
    TEST_ASSERT_EQUAL_HEX32(EL_MB_FX_ADDRESS | EL_MB_FX_BAUD | EL_MB_FX_PARITY, fx.mask);
}

/* ---------- gate: busy / device failure ---------- */

static el_mb_verdict_t g_verdict;
static int g_gate_calls;
static el_mb_verdict_t gate_cb(void *ctx, const el_mb_effects_t *fx)
{
    TEST_ASSERT_EQUAL_PTR(&g_gate_calls, ctx);
    TEST_ASSERT_TRUE(fx->mask != 0);
    g_gate_calls++;
    return g_verdict;
}

static void test_gate_busy_and_failure(void)
{
    fresh();
    el_mb_set_gate(&g, gate_cb, &g_gate_calls);
    g_gate_calls = 0;
    g_verdict = EL_MB_REJECT_BUSY;
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_BUSY, write1(EL_MB_HR_COMMAND, EL_MB_CMD_START));
    g_verdict = EL_MB_REJECT_FAILURE;
    TEST_ASSERT_EQUAL_UINT8(EL_MB_EX_DEVICE_FAILURE, write1(EL_MB_HR_REPEATS, 4));
    TEST_ASSERT_EQUAL_UINT16(3, g.holding.repeats);              /* rejected -> not applied */
    el_mb_effects_t fx;
    TEST_ASSERT_FALSE(el_mb_take_effects(&g, &fx));
    g_verdict = EL_MB_ACCEPT;
    TEST_ASSERT_EQUAL_UINT8(0, write1(EL_MB_HR_REPEATS, 4));
    TEST_ASSERT_EQUAL_INT(3, g_gate_calls);
    TEST_ASSERT_TRUE(el_mb_take_effects(&g, &fx));
    /* command 0 and invalid values never reach the gate */
    write1(EL_MB_HR_COMMAND, 0);
    write1(EL_MB_HR_REPEATS, 99);
    TEST_ASSERT_EQUAL_INT(3, g_gate_calls);
    /* reads never touch the gate */
    el_mb_master_resp_t r;
    expect_read_ok(EL_MB_FC_READ_HOLDING, EL_MB_HR_REPEATS, 1, &r);
    TEST_ASSERT_EQUAL_INT(3, g_gate_calls);
}

/* ---------- addressing, broadcast, CRC, stats ---------- */

static void test_broadcast_write_applied_without_response(void)
{
    fresh();
    uint8_t req[16], resp[EL_MB_RTU_MAX_FRAME];
    size_t n = el_mb_master_write_single(req, sizeof req, 0, EL_MB_HR_REPEATS, 7);
    TEST_ASSERT_EQUAL(0, el_mb_rtu_process(&g, req, n, resp, sizeof resp));
    TEST_ASSERT_EQUAL_UINT16(7, g.holding.repeats);
    el_mb_effects_t fx;
    TEST_ASSERT_TRUE(el_mb_take_effects(&g, &fx));
    uint16_t v[2];
    el_mb_put_u32(v, 0);
    n = el_mb_master_write_multi(req, sizeof req, 0, EL_MB_HR_ADDRESS, (uint16_t[]){9}, 1);
    TEST_ASSERT_EQUAL(0, el_mb_rtu_process(&g, req, n, resp, sizeof resp));
    TEST_ASSERT_EQUAL_UINT16(9, g.holding.address);
    /* broadcast write with an invalid value: nothing applied, still silent, no exception counted */
    n = el_mb_master_write_single(req, sizeof req, 0, EL_MB_HR_REPEATS, 99);
    TEST_ASSERT_EQUAL(0, el_mb_rtu_process(&g, req, n, resp, sizeof resp));
    TEST_ASSERT_EQUAL_UINT16(7, g.holding.repeats);
    /* broadcast reads / diagnostics / unknown functions are ignored */
    n = el_mb_master_read(req, sizeof req, 0, EL_MB_FC_READ_INPUT, 0, 2);
    TEST_ASSERT_EQUAL(0, el_mb_rtu_process(&g, req, n, resp, sizeof resp));
    TEST_ASSERT_EQUAL_UINT32(4, g.stats.broadcasts);
    TEST_ASSERT_EQUAL_UINT32(0, g.stats.responses_tx);
    TEST_ASSERT_EQUAL_UINT32(0, g.stats.exceptions_tx);
}

static void test_wrong_address_ignored_and_counted(void)
{
    fresh();
    uint8_t req[16], resp[EL_MB_RTU_MAX_FRAME];
    size_t n = el_mb_master_write_single(req, sizeof req, ADDR + 1, EL_MB_HR_REPEATS, 7);
    TEST_ASSERT_EQUAL(0, el_mb_rtu_process(&g, req, n, resp, sizeof resp));
    TEST_ASSERT_EQUAL_UINT16(3, g.holding.repeats);
    n = el_mb_master_read(req, sizeof req, 247, EL_MB_FC_READ_INPUT, 0, 2);
    TEST_ASSERT_EQUAL(0, el_mb_rtu_process(&g, req, n, resp, sizeof resp));
    TEST_ASSERT_EQUAL_UINT32(2, g.stats.other_address);
    TEST_ASSERT_EQUAL_UINT32(2, g.stats.frames_rx);
}

static void test_bad_crc_ignored_and_counted(void)
{
    fresh();
    uint8_t req[16], resp[EL_MB_RTU_MAX_FRAME];
    size_t n = el_mb_master_write_single(req, sizeof req, ADDR, EL_MB_HR_REPEATS, 7);
    req[n - 1] ^= 0x01;
    TEST_ASSERT_EQUAL(0, el_mb_rtu_process(&g, req, n, resp, sizeof resp));
    n = el_mb_master_write_single(req, sizeof req, ADDR, EL_MB_HR_REPEATS, 7);
    req[3] ^= 0x80;                                    /* corrupted payload */
    TEST_ASSERT_EQUAL(0, el_mb_rtu_process(&g, req, n, resp, sizeof resp));
    TEST_ASSERT_EQUAL_UINT16(3, g.holding.repeats);
    TEST_ASSERT_EQUAL_UINT32(2, g.stats.crc_errors);
    /* a bad CRC on a frame for another address is a CRC error first (we cannot trust the address) */
    n = el_mb_master_read(req, sizeof req, 99, EL_MB_FC_READ_INPUT, 0, 2);
    req[0] ^= 0x04;
    el_mb_rtu_process(&g, req, n, resp, sizeof resp);
    TEST_ASSERT_EQUAL_UINT32(3, g.stats.crc_errors);
    TEST_ASSERT_EQUAL_UINT32(0, g.stats.other_address);
}

static void test_short_and_long_frames(void)
{
    fresh();
    uint8_t req[300] = {0}, resp[EL_MB_RTU_MAX_FRAME];
    for (size_t n = 0; n < 4; n++) TEST_ASSERT_EQUAL(0, el_mb_rtu_process(&g, req, n, resp, sizeof resp));
    TEST_ASSERT_EQUAL(0, el_mb_rtu_process(&g, req, 257, resp, sizeof resp));
    TEST_ASSERT_EQUAL_UINT32(5, g.stats.bad_length);
    TEST_ASSERT_EQUAL(0, el_mb_rtu_process(NULL, req, 8, resp, sizeof resp));
    TEST_ASSERT_EQUAL(0, el_mb_rtu_process(&g, NULL, 8, resp, sizeof resp));
    TEST_ASSERT_EQUAL(0, el_mb_rtu_process(&g, req, 8, NULL, 0));
    /* minimum valid frame: address, function, CRC (unknown function) -> exception 01 */
    uint8_t m[4] = {ADDR, 0x41};
    el_mb_frame_seal(m, 2);
    size_t l = el_mb_rtu_process(&g, m, 4, resp, sizeof resp);
    TEST_ASSERT_EQUAL(5, l);
    TEST_ASSERT_EQUAL_HEX8(0xC1, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(EL_MB_EX_FUNCTION, resp[2]);
}

static void test_stats_counters(void)
{
    fresh();
    uint8_t req[16], resp[EL_MB_RTU_MAX_FRAME];
    size_t n = el_mb_master_read(req, sizeof req, ADDR, EL_MB_FC_READ_INPUT, 0, 2);
    TEST_ASSERT_TRUE(el_mb_rtu_process(&g, req, n, resp, sizeof resp) > 0);
    n = el_mb_master_read(req, sizeof req, ADDR, EL_MB_FC_READ_INPUT, 1, 1);
    TEST_ASSERT_TRUE(el_mb_rtu_process(&g, req, n, resp, sizeof resp) > 0);
    TEST_ASSERT_EQUAL_UINT32(2, g.stats.frames_rx);
    TEST_ASSERT_EQUAL_UINT32(1, g.stats.responses_tx);
    TEST_ASSERT_EQUAL_UINT32(1, g.stats.exceptions_tx);
}

static void test_response_buffer_too_small_is_silent(void)
{
    fresh();
    uint8_t req[16], resp[EL_MB_RTU_MAX_FRAME];
    size_t n = el_mb_master_read(req, sizeof req, ADDR, EL_MB_FC_READ_INPUT, EL_MB_IR_TEMP, 2);
    memset(resp, 0xA5, sizeof resp);
    TEST_ASSERT_EQUAL(0, el_mb_rtu_process(&g, req, n, resp, 8));         /* needs 9 */
    for (size_t i = 0; i < sizeof resp; i++) TEST_ASSERT_EQUAL_HEX8(0xA5, resp[i]);
    TEST_ASSERT_EQUAL(9, el_mb_rtu_process(&g, req, n, resp, 9));
    TEST_ASSERT_EQUAL_HEX8(0xA5, resp[9]);
}

static void test_read_response_wire_bytes(void)
{
    fresh();
    uint8_t req[8], resp[16];
    size_t n = el_mb_master_read(req, sizeof req, ADDR, EL_MB_FC_READ_INPUT, EL_MB_IR_STATUS, 2);
    size_t l = el_mb_rtu_process(&g, req, n, resp, sizeof resp);
    TEST_ASSERT_EQUAL(9, l);
    const uint8_t head[] = {ADDR, 0x04, 0x04, 0x00, 0x02, 0x00, 0x07};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(head, resp, sizeof head);
    TEST_ASSERT_EQUAL_HEX16(el_mb_crc16(resp, 7), (uint16_t)(resp[7] | (resp[8] << 8)));
}

/* ---------- FC08 and FC43/14 ---------- */

static void test_diagnostic_echo(void)
{
    fresh();
    uint8_t req[8] = {ADDR, 0x08, 0x00, 0x00, 0xA5, 0x37}, resp[EL_MB_RTU_MAX_FRAME];
    size_t n = el_mb_frame_seal(req, 6);
    size_t l = el_mb_rtu_process(&g, req, n, resp, sizeof resp);
    TEST_ASSERT_EQUAL(8, l);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(req, resp, 8);
    /* other sub-functions: illegal function; wrong length: illegal value */
    req[3] = 0x01;
    n = el_mb_frame_seal(req, 6);
    l = el_mb_rtu_process(&g, req, n, resp, sizeof resp);
    TEST_ASSERT_EQUAL(5, l);
    TEST_ASSERT_EQUAL_HEX8(0x88, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(EL_MB_EX_FUNCTION, resp[2]);
    uint8_t shortq[6] = {ADDR, 0x08, 0x00, 0x00};
    n = el_mb_frame_seal(shortq, 4);
    l = el_mb_rtu_process(&g, shortq, n, resp, sizeof resp);
    TEST_ASSERT_EQUAL_HEX8(EL_MB_EX_VALUE, resp[2]);
}

static void test_device_identification(void)
{
    fresh();
    uint8_t req[8] = {ADDR, 0x2B, 0x0E, 0x01, 0x00}, resp[EL_MB_RTU_MAX_FRAME];
    size_t n = el_mb_frame_seal(req, 5);
    size_t l = el_mb_rtu_process(&g, req, n, resp, sizeof resp);
    TEST_ASSERT_TRUE(l > 10);
    TEST_ASSERT_EQUAL_HEX8(0x2B, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(0x0E, resp[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, resp[3]);       /* echoed code */
    TEST_ASSERT_EQUAL_HEX8(0x00, resp[5]);       /* no more follows */
    TEST_ASSERT_EQUAL_HEX8(3, resp[7]);          /* 3 objects */
    TEST_ASSERT_EQUAL_HEX8(0, resp[8]);
    size_t vn = strlen(EL_MB_VENDOR), pc = strlen(EL_MB_PRODUCT_CODE);
    TEST_ASSERT_EQUAL_HEX8(vn, resp[9]);
    TEST_ASSERT_EQUAL_MEMORY(EL_MB_VENDOR, resp + 10, vn);
    TEST_ASSERT_EQUAL_HEX8(1, resp[10 + vn]);
    TEST_ASSERT_EQUAL_HEX8(pc, resp[11 + vn]);
    TEST_ASSERT_EQUAL_MEMORY(EL_MB_PRODUCT_CODE, resp + 12 + vn, pc);
    TEST_ASSERT_EQUAL_HEX8(2, resp[12 + vn + pc]);
    TEST_ASSERT_EQUAL_HEX8(5, resp[13 + vn + pc]);
    TEST_ASSERT_EQUAL_MEMORY("1.2.3", resp + 14 + vn + pc, 5);
    TEST_ASSERT_EQUAL(14 + vn + pc + 5 + 2, l);

    /* individual access: object 2 only; unknown object -> 02; unknown stream start restarts */
    req[3] = 0x04; req[4] = 0x02;
    n = el_mb_frame_seal(req, 5);
    l = el_mb_rtu_process(&g, req, n, resp, sizeof resp);
    TEST_ASSERT_EQUAL_HEX8(1, resp[7]);
    TEST_ASSERT_EQUAL_HEX8(2, resp[8]);
    req[4] = 0x09;
    n = el_mb_frame_seal(req, 5);
    l = el_mb_rtu_process(&g, req, n, resp, sizeof resp);
    TEST_ASSERT_EQUAL(5, l);
    TEST_ASSERT_EQUAL_HEX8(EL_MB_EX_ADDRESS, resp[2]);
    req[3] = 0x01;
    n = el_mb_frame_seal(req, 5);
    l = el_mb_rtu_process(&g, req, n, resp, sizeof resp);
    TEST_ASSERT_EQUAL_HEX8(3, resp[7]);
    /* regular/extended categories and bad codes -> 03; other MEI type -> 01 */
    uint8_t codes[] = {0, 2, 3, 5, 0xFF};
    for (size_t i = 0; i < sizeof codes; i++) {
        req[3] = codes[i]; req[4] = 0;
        n = el_mb_frame_seal(req, 5);
        l = el_mb_rtu_process(&g, req, n, resp, sizeof resp);
        TEST_ASSERT_EQUAL_HEX8(EL_MB_EX_VALUE, resp[2]);
    }
    req[2] = 0x0D; req[3] = 1;
    n = el_mb_frame_seal(req, 5);
    l = el_mb_rtu_process(&g, req, n, resp, sizeof resp);
    TEST_ASSERT_EQUAL_HEX8(EL_MB_EX_FUNCTION, resp[2]);
    (void)l;
}

/* ---------- master helper ---------- */

static void test_master_parse_errors(void)
{
    el_mb_master_resp_t r;
    uint8_t f[16] = {1, 3, 2, 0x12, 0x34};
    size_t n = el_mb_frame_seal(f, 5);
    TEST_ASSERT_EQUAL(EL_OK, el_mb_master_parse(f, n, 1, 3, &r));
    TEST_ASSERT_EQUAL_HEX16(0x1234, r.regs[0]);
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_mb_master_parse(f, n, 2, 3, &r));     /* wrong address */
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_mb_master_parse(f, n, 1, 4, &r));     /* wrong function */
    f[n - 1] ^= 1;
    TEST_ASSERT_EQUAL(EL_ERR_CRC, el_mb_master_parse(f, n, 1, 3, &r));
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_mb_master_parse(f, 4, 1, 3, &r));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_mb_master_parse(NULL, n, 1, 3, &r));
    uint8_t odd[16] = {1, 3, 3, 1, 2, 3};                                      /* odd byte count */
    n = el_mb_frame_seal(odd, 6);
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_mb_master_parse(odd, n, 1, 3, &r));
    uint8_t lie[16] = {1, 3, 4, 1, 2};                                         /* count > data */
    n = el_mb_frame_seal(lie, 5);
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_mb_master_parse(lie, n, 1, 3, &r));
    uint8_t exc[8] = {1, 0x83, 2};
    n = el_mb_frame_seal(exc, 3);
    TEST_ASSERT_EQUAL(EL_OK, el_mb_master_parse(exc, n, 1, 3, &r));
    TEST_ASSERT_EQUAL_UINT8(2, r.exception);
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_mb_master_parse(exc, n, 1, 4, &r));
}

static void test_init_validation(void)
{
    el_mb_t m;
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_mb_init(NULL, 1));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_mb_init(&m, 0));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_mb_init(&m, 248));
    TEST_ASSERT_EQUAL(EL_OK, el_mb_init(&m, 247));
}

/* ---------- fuzz ---------- */

static uint32_t rng_state;
static uint32_t rnd(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

static void test_fuzz_never_crashes_or_overruns(void)
{
    enum { GUARD = 32 };
    static uint8_t out[EL_MB_RTU_MAX_FRAME + 2 * GUARD];
    uint8_t req[300];
    fresh();
    rng_state = 0xC0FFEE;
    static const uint8_t fcs[] = {3, 4, 6, 16, 8, 0x2B, 1, 5, 0x80};
    size_t responded = 0;
    for (int it = 0; it < 10000; it++) {
        size_t len = rnd() % 300;
        for (size_t i = 0; i < len; i++) req[i] = (uint8_t)rnd();
        if (it & 1) {                                   /* half: valid CRC and plausible header */
            if (len < 4) len = 4 + (rnd() % 20);
            req[0] = (rnd() % 8 == 0) ? 0 : ADDR;
            req[1] = fcs[rnd() % sizeof fcs];
            if (rnd() % 2) { req[2] = (uint8_t)(rnd() % 3 ? 0 : rnd()); req[3] = (uint8_t)(rnd() % 150); }
            if (len > 254) len = 254;
            el_mb_frame_seal(req, len - 2);
        }
        size_t cap = rnd() % (EL_MB_RTU_MAX_FRAME + 1);
        memset(out, 0xEE, sizeof out);
        size_t l = el_mb_rtu_process(&g, req, len, out + GUARD, cap);
        TEST_ASSERT_TRUE(l <= cap);
        for (size_t i = 0; i < GUARD; i++) TEST_ASSERT_EQUAL_HEX8(0xEE, out[i]);
        for (size_t i = GUARD + cap; i < sizeof out; i++) TEST_ASSERT_EQUAL_HEX8(0xEE, out[i]);
        if (l) {
            responded++;
            TEST_ASSERT_TRUE(l >= 5);
            TEST_ASSERT_EQUAL_HEX16(el_mb_crc16(out + GUARD, l - 2),
                                    (uint16_t)(out[GUARD + l - 2] | (out[GUARD + l - 1] << 8)));
        }
        /* settings stay inside their validated ranges whatever the garbage did */
        TEST_ASSERT_TRUE(g.holding.repeats >= 1 && g.holding.repeats <= 10);
        TEST_ASSERT_TRUE(g.holding.address >= 1 && g.holding.address <= 247);
        TEST_ASSERT_TRUE(g.holding.baud_code <= 4 && g.holding.parity_code <= 2);
        const el_limits_t *lim = &g.holding.limits;
        TEST_ASSERT_TRUE(lim->t_low_c >= -40.0f && lim->t_high_c <= 125.0f && lim->t_low_c < lim->t_high_c);
        TEST_ASSERT_TRUE(lim->rh_low_pct >= 0.0f && lim->rh_high_pct <= 100.0f && lim->rh_low_pct < lim->rh_high_pct);
        TEST_ASSERT_TRUE(g.holding.log_interval_s == 0 ||
                         (g.holding.log_interval_s >= 10 && g.holding.log_interval_s <= 86400));
    }
    TEST_ASSERT_TRUE(responded > 100);                  /* the fuzz did reach real handlers */
    TEST_ASSERT_EQUAL_UINT32(10000, g.stats.frames_rx);
}

void run_modbus_tests(void)
{
    RUN_TEST(test_crc_known_frames);
    RUN_TEST(test_master_builders_produce_known_bytes);
    RUN_TEST(test_read_input_registers_full_map);
    RUN_TEST(test_float_nan_round_trip);
    RUN_TEST(test_string_registers);
    RUN_TEST(test_get_str_rejects_bad_data);
    RUN_TEST(test_read_holding_defaults_and_command_reads_zero);
    RUN_TEST(test_maximum_read_quantity);
    RUN_TEST(test_exception_illegal_function);
    RUN_TEST(test_exception_address_and_gaps);
    RUN_TEST(test_address_range_end);
    RUN_TEST(test_exception_value_quantities);
    RUN_TEST(test_max_write_quantity_is_accepted_in_framing);
    RUN_TEST(test_value_validation_each_setting);
    RUN_TEST(test_write_each_setting_reports_effect);
    RUN_TEST(test_commands_are_reported_not_stored);
    RUN_TEST(test_write_limits_float_pair);
    RUN_TEST(test_write_log_interval);
    RUN_TEST(test_multi_write_is_atomic);
    RUN_TEST(test_gate_busy_and_failure);
    RUN_TEST(test_broadcast_write_applied_without_response);
    RUN_TEST(test_wrong_address_ignored_and_counted);
    RUN_TEST(test_bad_crc_ignored_and_counted);
    RUN_TEST(test_short_and_long_frames);
    RUN_TEST(test_stats_counters);
    RUN_TEST(test_response_buffer_too_small_is_silent);
    RUN_TEST(test_read_response_wire_bytes);
    RUN_TEST(test_diagnostic_echo);
    RUN_TEST(test_device_identification);
    RUN_TEST(test_master_parse_errors);
    RUN_TEST(test_init_validation);
    RUN_TEST(test_fuzz_never_crashes_or_overruns);
}
