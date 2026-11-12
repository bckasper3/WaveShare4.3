#include <math.h>
#include <string.h>
#include "unity.h"
#include "el_sensor_sht.h"

/* ---- fake I2C bus: records the last write, serves a canned 6-byte reply ---- */
typedef struct {
    bool    present;          /* device ACKs its address */
    uint8_t addr;
    uint8_t last_cmd[2];
    size_t  last_len;
    int     writes, reads;
    int     busy_reads;       /* NACK this many reads first (conversion still running) */
    uint8_t reply[6];
} fake_bus_t;

static int fake_write(void *bus, uint8_t addr, const uint8_t *d, size_t n)
{
    fake_bus_t *b = bus;
    if (!b->present || addr != b->addr) return -1;
    b->writes++;
    b->last_len = n;
    memcpy(b->last_cmd, d, n > 2 ? 2 : n);
    return 0;
}

static int fake_read(void *bus, uint8_t addr, uint8_t *d, size_t n)
{
    fake_bus_t *b = bus;
    if (!b->present || addr != b->addr) return -1;
    b->reads++;
    if (b->busy_reads > 0) { b->busy_reads--; return -1; }
    memcpy(d, b->reply, n);
    return 0;
}

/* Build a reply with correct CRCs. */
static void make_reply(uint8_t r[6], uint16_t t_raw, uint16_t h_raw)
{
    r[0] = (uint8_t)(t_raw >> 8); r[1] = (uint8_t)t_raw; r[2] = el_sht_crc8(r, 2);
    r[3] = (uint8_t)(h_raw >> 8); r[4] = (uint8_t)h_raw; r[5] = el_sht_crc8(r + 3, 2);
}

static fake_bus_t bus;
static el_sht_t sht;
static el_sensor_t drv;

static void setup(el_sht_model_t m, uint8_t addr)
{
    memset(&bus, 0, sizeof bus);
    bus.present = true;
    bus.addr = addr;
    el_sht_io_t io = { fake_write, fake_read, &bus };
    TEST_ASSERT_EQUAL(EL_OK, el_sht_init(&sht, m, addr, &io));
    drv = el_sht_driver(&sht);
    TEST_ASSERT_TRUE(el_sensor_valid(&drv));
}

static void test_crc8_datasheet_check_value(void)
{
    const uint8_t beef[2] = { 0xBE, 0xEF };            /* Sensirion datasheets: CRC(0xBEEF) = 0x92 */
    TEST_ASSERT_EQUAL_HEX8(0x92, el_sht_crc8(beef, 2));
    const uint8_t zero[2] = { 0x00, 0x00 };
    TEST_ASSERT_EQUAL_HEX8(0x81, el_sht_crc8(zero, 2));
    TEST_ASSERT_EQUAL_HEX8(0xFF, el_sht_crc8(zero, 0));
}

static void test_conversion_formulas(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -45.0f, el_sht_temp_c(0));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 130.0f, el_sht_temp_c(0xFFFF));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, el_sht_temp_c(0x6666));   /* 26214 -> 25.0 degC */
    /* SHT4x: RH = -6 + 125 * raw / 65535, cropped to 0..100 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 54.0f, el_sht_rh_pct(EL_SHT4X, 0x7AE1));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, el_sht_rh_pct(EL_SHT4X, 0));          /* -6 % cropped */
    TEST_ASSERT_EQUAL_FLOAT(100.0f, el_sht_rh_pct(EL_SHT4X, 0xFFFF));   /* 119 % cropped */
    /* SHT3x: RH = 100 * raw / 65535 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, el_sht_rh_pct(EL_SHT3X, 0x8000));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, el_sht_rh_pct(EL_SHT3X, 0xFFFF));
}

static void test_decode_checks_both_crcs(void)
{
    uint8_t r[6];
    make_reply(r, 0x6666, 0x8000);
    float t = 0, h = 0;
    TEST_ASSERT_EQUAL(EL_SENSOR_OK, el_sht_decode(EL_SHT3X, r, &t, &h));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, t);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, h);
    for (int i = 0; i < 6; i++) {                       /* any corrupted byte is caught */
        uint8_t b[6];
        memcpy(b, r, 6);
        b[i] ^= 0x10;
        t = h = -1.0f;
        TEST_ASSERT_EQUAL(EL_SENSOR_ERR_CRC, el_sht_decode(EL_SHT3X, b, &t, &h));
        TEST_ASSERT_EQUAL_FLOAT(-1.0f, t);              /* outputs untouched */
    }
    TEST_ASSERT_EQUAL(EL_SENSOR_ERR_INTERNAL, el_sht_decode(EL_SHT4X, NULL, &t, &h));
}

static void test_sht4x_measurement_cycle(void)
{
    setup(EL_SHT4X, 0x44);
    make_reply(bus.reply, 0x6666, 0x7AE1);
    el_sensor_sample_t s; el_sensor_err_t e;
    TEST_ASSERT_EQUAL(EL_SENSOR_OK, drv.ops->begin_conversion(drv.ctx, 1000));
    TEST_ASSERT_EQUAL_size_t(1, bus.last_len);
    TEST_ASSERT_EQUAL_HEX8(0xFD, bus.last_cmd[0]);
    TEST_ASSERT_EQUAL(EL_SENSOR_PENDING, drv.ops->poll(drv.ctx, 1009, &s, &e));
    TEST_ASSERT_EQUAL_INT(0, bus.reads);                /* no read before the conversion time */
    TEST_ASSERT_EQUAL(EL_SENSOR_READY, drv.ops->poll(drv.ctx, 1010, &s, &e));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, s.temp_c);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 54.0f, s.rh_pct);
    TEST_ASSERT_EQUAL_HEX8(EL_SAMPLE_VALID, s.quality);
    TEST_ASSERT_EQUAL(EL_SENSOR_ERROR, drv.ops->poll(drv.ctx, 1020, &s, &e));   /* idle again */
    TEST_ASSERT_EQUAL(EL_SENSOR_ERR_INTERNAL, e);
    el_sensor_caps_t c;
    drv.ops->caps(drv.ctx, &c);
    TEST_ASSERT_EQUAL_HEX32(0, c.flags);
    TEST_ASSERT_EQUAL_UINT32(EL_SHT4X_WAIT_MS, c.typ_conversion_ms);
    char b[32];
    TEST_ASSERT_EQUAL(EL_OK, drv.ops->info(drv.ctx, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("SHT4x @0x44", b);
}

static void test_sht3x_command_and_busy_nack(void)
{
    setup(EL_SHT3X, 0x45);
    make_reply(bus.reply, 0x6666, 0x8000);
    bus.busy_reads = 2;                                 /* still converting: address NACKed */
    el_sensor_sample_t s; el_sensor_err_t e;
    TEST_ASSERT_EQUAL(EL_SENSOR_OK, drv.ops->begin_conversion(drv.ctx, 0));
    TEST_ASSERT_EQUAL_size_t(2, bus.last_len);
    TEST_ASSERT_EQUAL_HEX8(0x24, bus.last_cmd[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, bus.last_cmd[1]);
    TEST_ASSERT_EQUAL(EL_SENSOR_PENDING, drv.ops->poll(drv.ctx, 15, &s, &e));
    TEST_ASSERT_EQUAL(EL_SENSOR_PENDING, drv.ops->poll(drv.ctx, 16, &s, &e));
    TEST_ASSERT_EQUAL(EL_SENSOR_PENDING, drv.ops->poll(drv.ctx, 20, &s, &e));
    TEST_ASSERT_EQUAL(EL_SENSOR_READY, drv.ops->poll(drv.ctx, 25, &s, &e));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, s.rh_pct);
    char b[32];
    drv.ops->info(drv.ctx, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("SHT3x @0x45", b);
}

static void test_errors_no_ack_timeout_crc(void)
{
    setup(EL_SHT4X, 0x44);
    el_sensor_sample_t s; el_sensor_err_t e;
    bus.present = false;                                /* unplugged */
    TEST_ASSERT_EQUAL(EL_SENSOR_ERR_NO_ACK, drv.ops->begin_conversion(drv.ctx, 0));
    bus.present = true;
    bus.busy_reads = 1000;                              /* never answers */
    TEST_ASSERT_EQUAL(EL_SENSOR_OK, drv.ops->begin_conversion(drv.ctx, 0));
    TEST_ASSERT_EQUAL(EL_SENSOR_PENDING, drv.ops->poll(drv.ctx, 50, &s, &e));
    TEST_ASSERT_EQUAL(EL_SENSOR_ERROR, drv.ops->poll(drv.ctx, EL_SHT_READ_TIMEOUT_MS, &s, &e));
    TEST_ASSERT_EQUAL(EL_SENSOR_ERR_TIMEOUT, e);
    bus.busy_reads = 0;
    make_reply(bus.reply, 0x6666, 0x8000);
    bus.reply[4] ^= 0x01;                               /* bit error on the humidity word */
    TEST_ASSERT_EQUAL(EL_SENSOR_OK, drv.ops->begin_conversion(drv.ctx, 200));
    TEST_ASSERT_EQUAL(EL_SENSOR_ERROR, drv.ops->poll(drv.ctx, 210, &s, &e));
    TEST_ASSERT_EQUAL(EL_SENSOR_ERR_CRC, e);
    /* abort leaves it idle */
    TEST_ASSERT_EQUAL(EL_SENSOR_OK, drv.ops->begin_conversion(drv.ctx, 300));
    drv.ops->abort(drv.ctx);
    TEST_ASSERT_EQUAL(EL_SENSOR_ERROR, drv.ops->poll(drv.ctx, 400, &s, &e));
    TEST_ASSERT_EQUAL(EL_SENSOR_ERR_INTERNAL, e);
}

static void test_init_arguments_and_commands(void)
{
    el_sht_io_t io = { fake_write, fake_read, &bus };
    el_sht_io_t half = { fake_write, NULL, &bus };
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_sht_init(&sht, EL_SHT4X, 0x40, &io));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_sht_init(&sht, (el_sht_model_t)7, 0x44, &io));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_sht_init(&sht, EL_SHT4X, 0x44, &half));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_sht_init(NULL, EL_SHT4X, 0x44, &io));
    size_t n = 0;
    const uint8_t *c = el_sht_reset_cmd(EL_SHT4X, &n);
    TEST_ASSERT_EQUAL_size_t(1, n); TEST_ASSERT_EQUAL_HEX8(0x94, c[0]);
    c = el_sht_reset_cmd(EL_SHT3X, &n);
    TEST_ASSERT_EQUAL_size_t(2, n); TEST_ASSERT_EQUAL_HEX8(0x30, c[0]); TEST_ASSERT_EQUAL_HEX8(0xA2, c[1]);
    TEST_ASSERT_EQUAL_STRING("SHT4x", el_sht_model_name(EL_SHT4X));
    TEST_ASSERT_EQUAL_STRING("SHT3x", el_sht_model_name(EL_SHT3X));
}

void run_sensor_sht_tests(void)
{
    RUN_TEST(test_crc8_datasheet_check_value);
    RUN_TEST(test_conversion_formulas);
    RUN_TEST(test_decode_checks_both_crcs);
    RUN_TEST(test_sht4x_measurement_cycle);
    RUN_TEST(test_sht3x_command_and_busy_nack);
    RUN_TEST(test_errors_no_ack_timeout_crc);
    RUN_TEST(test_init_arguments_and_commands);
}
