#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "el_settings.h"
#include "el_crc.h"
#include "el_util.h"

static el_settings_t defaults(void)
{
    el_settings_t s;
    el_settings_defaults(&s);
    return s;
}

static void set_ok(el_settings_t *s, el_set_id_t id, const char *text)
{
    char msg[80];
    snprintf(msg, sizeof msg, "setting %s=%s", el_settings_def(id)->key, text);
    TEST_ASSERT_EQUAL_MESSAGE(EL_OK, el_settings_set_str(s, id, text, EL_LEVEL_ENGINEER), msg);
}

static void expect_get(const el_settings_t *s, el_set_id_t id, const char *want)
{
    char b[64];
    TEST_ASSERT_EQUAL(EL_OK, el_settings_get_str(s, id, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING(want, b);
}

/* Re-sign a hand-modified encoding so only the field under test is "wrong". */
static void resign(uint8_t *b, size_t len) { el_put_u32le(b + len - 4, el_crc32(b, len - 4)); }

/* Offset of a tag's TLV header inside an encoding, or 0 if absent. */
static size_t find_tlv(const uint8_t *b, size_t len, uint16_t tag)
{
    size_t end = 6 + el_get_u16le(b + 4);
    for (size_t p = 6; p < end && p + 3 <= len; p += 3 + b[p + 2])
        if (el_get_u16le(b + p) == tag) return p;
    return 0;
}

static void test_defaults_match_spec_and_validate(void)
{
    el_settings_t s = defaults();
    TEST_ASSERT_EQUAL(EL_OK, el_settings_validate(&s));
    expect_get(&s, EL_SET_DEVICE_SERIAL, "EL-0001");
    expect_get(&s, EL_SET_REPEATS, "3");
    expect_get(&s, EL_SET_DECIMALS, "1");
    expect_get(&s, EL_SET_LOG_INTERVAL, "300");
    expect_get(&s, EL_SET_LOCATION, "");
    expect_get(&s, EL_SET_T_LOW, "5");
    expect_get(&s, EL_SET_T_HIGH, "35");
    expect_get(&s, EL_SET_RH_LOW, "20");
    expect_get(&s, EL_SET_RH_HIGH, "80");
    expect_get(&s, EL_SET_T_OFFSET, "0");
    expect_get(&s, EL_SET_RH_OFFSET, "0");
    expect_get(&s, EL_SET_SENSOR_TYPE, "SHT4X");
    expect_get(&s, EL_SET_SENSOR_ADDR, "0X44");
    expect_get(&s, EL_SET_BRIGHTNESS, "100");
    expect_get(&s, EL_SET_IDLE_DIM, "120");
    expect_get(&s, EL_SET_IDLE_OFF, "600");
    expect_get(&s, EL_SET_DATE_FORMAT, "ISO");
    expect_get(&s, EL_SET_UTC_OFFSET, "0");
    expect_get(&s, EL_SET_MB_ADDR, "1");
    expect_get(&s, EL_SET_MB_BAUD, "19200");
    expect_get(&s, EL_SET_MB_PARITY, "EVEN");
    expect_get(&s, EL_SET_SERIAL_STREAM, "0");
    expect_get(&s, EL_SET_DO1_MODE, "ON_ALARM");
    expect_get(&s, EL_SET_DI0_TRIGGER, "0");
    expect_get(&s, EL_SET_ADMIN_PIN, "****");
    expect_get(&s, EL_SET_ENGINEER_CODE, "****");
    TEST_ASSERT_EQUAL_STRING(EL_DEFAULT_PIN, s.admin_pin);
    TEST_ASSERT_EQUAL_STRING(EL_DEFAULT_PIN, s.engineer_code);
    expect_get(&s, EL_SET_LOGIN_IDLE, "300");
    expect_get(&s, EL_SET_OPERATOR_ID, "");
    TEST_ASSERT_EQUAL_UINT32(19200, el_settings_mb_baud_bps(&s));
    el_limits_t l = el_settings_limits(&s);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, l.t_low_c);
    TEST_ASSERT_EQUAL_FLOAT(80.0f, l.rh_high_pct);
    l = el_settings_limits(NULL);                        /* everything off */
    TEST_ASSERT_EQUAL_FLOAT(-40.0f, l.t_low_c);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, l.rh_high_pct);
}

static void test_schema_is_consistent(void)
{
    TEST_ASSERT_NULL(el_settings_def(EL_SET_COUNT));
    for (int i = 0; i < EL_SET_COUNT; i++) {
        const el_set_def_t *a = el_settings_def((el_set_id_t)i);
        TEST_ASSERT_NOT_NULL(a);
        TEST_ASSERT_TRUE(a->key && a->key[0]);
        TEST_ASSERT_TRUE(a->tag != 0);
        TEST_ASSERT_TRUE(a->help && a->help[0]);
        TEST_ASSERT_TRUE(a->unit != NULL);
        TEST_ASSERT_EQUAL_MESSAGE(i, el_settings_find(a->key), a->key);   /* find() round-trips */
        TEST_ASSERT_TRUE(a->min <= a->max);
        if (a->type == EL_SET_T_ENUM) {
            TEST_ASSERT_TRUE(a->enum_count >= 2);
            TEST_ASSERT_EQUAL_FLOAT((float)(a->enum_count - 1), a->max);
        }
        for (const char *c = a->key; *c; c++)
            TEST_ASSERT_TRUE((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_');
        for (int j = i + 1; j < EL_SET_COUNT; j++) {
            const el_set_def_t *b = el_settings_def((el_set_id_t)j);
            TEST_ASSERT_TRUE_MESSAGE(strcmp(a->key, b->key) != 0, "duplicate key");
            TEST_ASSERT_TRUE_MESSAGE(a->tag != b->tag, "duplicate tag");
        }
    }
    TEST_ASSERT_EQUAL(EL_LEVEL_ENGINEER, el_settings_def(EL_SET_DEVICE_SERIAL)->write_level);
    TEST_ASSERT_EQUAL(EL_LEVEL_ENGINEER, el_settings_def(EL_SET_ENGINEER_CODE)->write_level);
    TEST_ASSERT_EQUAL(EL_LEVEL_ADMIN, el_settings_def(EL_SET_T_OFFSET)->write_level);
    TEST_ASSERT_EQUAL(EL_LEVEL_OPERATOR, el_settings_def(EL_SET_OPERATOR_ID)->write_level);
    TEST_ASSERT_EQUAL(EL_LEVEL_ADMIN, el_settings_def(EL_SET_REPEATS)->write_level);
    TEST_ASSERT_TRUE(el_settings_def(EL_SET_ADMIN_PIN)->secret);
    TEST_ASSERT_TRUE(el_settings_def(EL_SET_ENGINEER_CODE)->secret);
}

static void test_find_is_case_insensitive(void)
{
    TEST_ASSERT_EQUAL(EL_SET_MB_ADDR, el_settings_find("MB_ADDR"));
    TEST_ASSERT_EQUAL(EL_SET_MB_ADDR, el_settings_find("Mb_Addr"));
    TEST_ASSERT_EQUAL(EL_SET_COUNT, el_settings_find("mb_add"));
    TEST_ASSERT_EQUAL(EL_SET_COUNT, el_settings_find("mb_addr "));
    TEST_ASSERT_EQUAL(EL_SET_COUNT, el_settings_find(""));
    TEST_ASSERT_EQUAL(EL_SET_COUNT, el_settings_find(NULL));
}

static void test_set_get_round_trip_every_setting(void)
{
    static const struct { el_set_id_t id; const char *in, *out; } T[] = {
        { EL_SET_DEVICE_SERIAL, "EL-0042", "EL-0042" },
        { EL_SET_REPEATS, "5", "5" },          { EL_SET_DECIMALS, "2", "2" },
        { EL_SET_LOG_INTERVAL, "3600", "3600" }, { EL_SET_LOCATION, "Server room", "Server room" },
        { EL_SET_T_LOW, "-18.5", "-18.5" },    { EL_SET_T_HIGH, "-10", "-10" },
        { EL_SET_RH_LOW, "10", "10" },         { EL_SET_RH_HIGH, "95.5", "95.5" },
        { EL_SET_T_OFFSET, "-0.4", "-0.4" },   { EL_SET_RH_OFFSET, "2.5", "2.5" },
        { EL_SET_SENSOR_TYPE, "sht3x", "SHT3X" }, { EL_SET_SENSOR_ADDR, "0x45", "0X45" },
        { EL_SET_BRIGHTNESS, "40", "40" },
        { EL_SET_IDLE_DIM, "30", "30" },       { EL_SET_IDLE_OFF, "90", "90" },
        { EL_SET_DATE_FORMAT, "us", "US" },    { EL_SET_UTC_OFFSET, "-300", "-300" },
        { EL_SET_MB_ADDR, "17", "17" },        { EL_SET_MB_BAUD, "115200", "115200" },
        { EL_SET_MB_PARITY, "odd", "ODD" },    { EL_SET_SERIAL_STREAM, "on", "1" },
        { EL_SET_DO1_MODE, "busy", "BUSY" },   { EL_SET_DI0_TRIGGER, "TRUE", "1" },
        { EL_SET_ADMIN_PIN, "4321", "****" },  { EL_SET_LOGIN_IDLE, "600", "600" },
        { EL_SET_ENGINEER_CODE, "908172", "****" }, { EL_SET_OPERATOR_ID, "JD", "JD" },
        { EL_SET_THEME, "dark", "DARK" },      { EL_SET_HOME_RETURN, "60", "60" },
        { EL_SET_WIFI_ENABLED, "on", "1" },     { EL_SET_WIFI_SSID, "Plant WiFi", "Plant WiFi" },
        { EL_SET_WIFI_PASSWORD, "12345678", "****" }, { EL_SET_UPLOAD_URL, "https://a.example/x", "https://a.example/x" },
        { EL_SET_UPLOAD_KEY, "abc123", "****" }, { EL_SET_WIFI_TIME_SYNC, "1", "1" },
    };
    _Static_assert(sizeof T / sizeof T[0] == EL_SET_COUNT, "every setting covered");
    el_settings_t s = defaults();
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        set_ok(&s, T[i].id, T[i].in);
        expect_get(&s, T[i].id, T[i].out);
    }
    TEST_ASSERT_EQUAL(EL_OK, el_settings_validate(&s));
    TEST_ASSERT_EQUAL_UINT8(5, s.repeats);
    TEST_ASSERT_EQUAL_UINT32(3600, s.log_interval_s);
    TEST_ASSERT_EQUAL_FLOAT(-18.5f, s.temp_low_c);
    TEST_ASSERT_EQUAL_UINT8(EL_SENSOR_SHT3X, s.sensor_type);
    TEST_ASSERT_EQUAL_UINT8(EL_SENSOR_ADDR_45, s.sensor_addr);
    TEST_ASSERT_EQUAL_INT16(-300, s.utc_offset_min);
    TEST_ASSERT_EQUAL_UINT32(115200, el_settings_mb_baud_bps(&s));
    TEST_ASSERT_TRUE(s.serial_stream && s.di0_trigger);
    TEST_ASSERT_EQUAL_STRING("4321", s.admin_pin);
    TEST_ASSERT_EQUAL_STRING("908172", s.engineer_code);
    TEST_ASSERT_EQUAL_UINT16(600, s.login_idle_s);
}

static void test_boundaries_accepted_and_just_outside_rejected(void)
{
    el_settings_t s = defaults();
    set_ok(&s, EL_SET_REPEATS, "1");   set_ok(&s, EL_SET_REPEATS, "10");
    set_ok(&s, EL_SET_UTC_OFFSET, "-720"); set_ok(&s, EL_SET_UTC_OFFSET, "840");
    set_ok(&s, EL_SET_MB_ADDR, "247"); set_ok(&s, EL_SET_MB_ADDR, "1");
    set_ok(&s, EL_SET_T_LOW, "-40"); set_ok(&s, EL_SET_T_HIGH, "125");      /* both alarms off */
    set_ok(&s, EL_SET_RH_LOW, "0"); set_ok(&s, EL_SET_RH_HIGH, "100");
    set_ok(&s, EL_SET_T_OFFSET, "-10"); set_ok(&s, EL_SET_T_OFFSET, "10");
    set_ok(&s, EL_SET_RH_OFFSET, "-20"); set_ok(&s, EL_SET_RH_OFFSET, "20");
    set_ok(&s, EL_SET_LOG_INTERVAL, "0"); set_ok(&s, EL_SET_LOG_INTERVAL, "10");
    set_ok(&s, EL_SET_LOG_INTERVAL, "86400");
    set_ok(&s, EL_SET_ENGINEER_CODE, "0000"); set_ok(&s, EL_SET_ENGINEER_CODE, "12345678");
    set_ok(&s, EL_SET_LOCATION, "1234567890123456789");
    set_ok(&s, EL_SET_ADMIN_PIN, "12345678");
    set_ok(&s, EL_SET_DEVICE_SERIAL, "123456789012345");
    set_ok(&s, EL_SET_OPERATOR_ID, "1234567");
    set_ok(&s, EL_SET_HOME_RETURN, "0"); set_ok(&s, EL_SET_HOME_RETURN, "600");
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_set_str(&s, EL_SET_HOME_RETURN, "601", EL_LEVEL_ENGINEER));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_settings_set_str(&s, EL_SET_THEME, "BLUE", EL_LEVEL_ENGINEER));
    TEST_ASSERT_EQUAL(EL_OK, el_settings_validate(&s));
}

static void test_bad_values_rejected_and_struct_unchanged(void)
{
    static const struct { el_set_id_t id; const char *text; el_err_t err; } T[] = {
        { EL_SET_REPEATS, "0", EL_ERR_RANGE },  { EL_SET_REPEATS, "11", EL_ERR_RANGE },
        { EL_SET_REPEATS, "-1", EL_ERR_RANGE }, { EL_SET_REPEATS, "abc", EL_ERR_ARG },
        { EL_SET_REPEATS, "3.5", EL_ERR_ARG },  { EL_SET_REPEATS, "", EL_ERR_ARG },
        { EL_SET_REPEATS, "  ", EL_ERR_ARG },   { EL_SET_REPEATS, "-", EL_ERR_ARG },
        { EL_SET_REPEATS, "99999999999999", EL_ERR_RANGE },
        { EL_SET_DECIMALS, "4", EL_ERR_RANGE },
        { EL_SET_DECIMALS, "3", EL_ERR_RANGE },
        { EL_SET_T_OFFSET, "10.1", EL_ERR_RANGE }, { EL_SET_T_OFFSET, "-10.1", EL_ERR_RANGE },
        { EL_SET_T_OFFSET, "nan", EL_ERR_ARG },    { EL_SET_T_OFFSET, "inf", EL_ERR_ARG },
        { EL_SET_T_OFFSET, "1e40", EL_ERR_RANGE }, { EL_SET_T_OFFSET, "0x1", EL_ERR_ARG },
        { EL_SET_T_OFFSET, "0.9abc", EL_ERR_ARG },
        { EL_SET_RH_OFFSET, "20.5", EL_ERR_RANGE },
        { EL_SET_T_LOW, "-40.1", EL_ERR_RANGE },   { EL_SET_T_HIGH, "125.1", EL_ERR_RANGE },
        { EL_SET_RH_LOW, "-1", EL_ERR_RANGE },     { EL_SET_RH_HIGH, "101", EL_ERR_RANGE },
        { EL_SET_LOG_INTERVAL, "5", EL_ERR_RANGE }, { EL_SET_LOG_INTERVAL, "86401", EL_ERR_RANGE },
        { EL_SET_LOG_INTERVAL, "-10", EL_ERR_RANGE },
        { EL_SET_SENSOR_TYPE, "DHT22", EL_ERR_ARG }, { EL_SET_SENSOR_ADDR, "0x40", EL_ERR_ARG },
        { EL_SET_LOCATION, "12345678901234567890", EL_ERR_RANGE }, { EL_SET_LOCATION, "a\tb", EL_ERR_ARG },
        { EL_SET_ENGINEER_CODE, "123", EL_ERR_RANGE }, { EL_SET_ENGINEER_CODE, "12ab", EL_ERR_ARG },
        { EL_SET_BRIGHTNESS, "101", EL_ERR_RANGE },
        { EL_SET_IDLE_DIM, "3601", EL_ERR_RANGE }, { EL_SET_IDLE_OFF, "7201", EL_ERR_RANGE },
        { EL_SET_DATE_FORMAT, "JP", EL_ERR_ARG },
        { EL_SET_UTC_OFFSET, "-721", EL_ERR_RANGE }, { EL_SET_UTC_OFFSET, "841", EL_ERR_RANGE },
        { EL_SET_MB_ADDR, "0", EL_ERR_RANGE },  { EL_SET_MB_ADDR, "248", EL_ERR_RANGE },
        { EL_SET_MB_BAUD, "1200", EL_ERR_ARG }, { EL_SET_MB_PARITY, "MARK", EL_ERR_ARG },
        { EL_SET_SERIAL_STREAM, "maybe", EL_ERR_ARG }, { EL_SET_SERIAL_STREAM, "2", EL_ERR_ARG },
        { EL_SET_DO1_MODE, "x", EL_ERR_ARG },
        { EL_SET_ADMIN_PIN, "12a4", EL_ERR_ARG }, { EL_SET_ADMIN_PIN, "12 4", EL_ERR_ARG },
        { EL_SET_ADMIN_PIN, "-123", EL_ERR_ARG }, { EL_SET_ADMIN_PIN, "123456789", EL_ERR_RANGE },
        { EL_SET_LOGIN_IDLE, "59", EL_ERR_RANGE }, { EL_SET_LOGIN_IDLE, "3601", EL_ERR_RANGE },
        { EL_SET_DEVICE_SERIAL, "", EL_ERR_RANGE }, { EL_SET_DEVICE_SERIAL, "1234567890123456", EL_ERR_RANGE },
        { EL_SET_DEVICE_SERIAL, "EL\t1", EL_ERR_ARG },
        { EL_SET_OPERATOR_ID, "12345678", EL_ERR_RANGE }, { EL_SET_OPERATOR_ID, "BK\n", EL_ERR_ARG },
    };
    el_settings_t s = defaults();
    set_ok(&s, EL_SET_REPEATS, "4");                    /* make the baseline non-default */
    const el_settings_t before = s;
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        char msg[80];
        snprintf(msg, sizeof msg, "%s=\"%s\"", el_settings_def(T[i].id)->key, T[i].text);
        TEST_ASSERT_EQUAL_MESSAGE(T[i].err, el_settings_set_str(&s, T[i].id, T[i].text, EL_LEVEL_ENGINEER), msg);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&before, &s, sizeof s, msg);
    }
}

static void test_argument_errors(void)
{
    el_settings_t s = defaults();
    char b[8];
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_settings_set_str(NULL, EL_SET_REPEATS, "3", EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_settings_set_str(&s, EL_SET_REPEATS, NULL, EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_settings_set_str(&s, EL_SET_COUNT, "3", EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_settings_set_str(&s, EL_SET_REPEATS, "3", EL_LEVEL_COUNT));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_settings_get_str(NULL, EL_SET_REPEATS, b, sizeof b));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_settings_get_str(&s, EL_SET_COUNT, b, sizeof b));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_settings_get_str(&s, EL_SET_REPEATS, NULL, 8));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_settings_get_str(&s, EL_SET_REPEATS, b, 0));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_settings_validate(NULL));
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_settings_get_str(&s, EL_SET_DO1_MODE, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("ON_ALAR", b);                   /* truncated but terminated */
}

static void test_enum_and_bool_parsing(void)
{
    el_settings_t s = defaults();
    set_ok(&s, EL_SET_MB_PARITY, "none");   expect_get(&s, EL_SET_MB_PARITY, "NONE");
    set_ok(&s, EL_SET_MB_PARITY, "  OdD "); expect_get(&s, EL_SET_MB_PARITY, "ODD");
    set_ok(&s, EL_SET_DO1_MODE, "On_Alarm");
    TEST_ASSERT_EQUAL_UINT8(EL_DO1_ON_ALARM, s.do1_mode);
    set_ok(&s, EL_SET_DO1_MODE, "off");     TEST_ASSERT_EQUAL_UINT8(EL_DO1_OFF, s.do1_mode);
    set_ok(&s, EL_SET_MB_BAUD, "9600");     TEST_ASSERT_EQUAL_UINT32(9600, el_settings_mb_baud_bps(&s));
    set_ok(&s, EL_SET_MB_BAUD, "57600");    TEST_ASSERT_EQUAL_UINT32(57600, el_settings_mb_baud_bps(&s));
    set_ok(&s, EL_SET_DATE_FORMAT, "eu");   TEST_ASSERT_EQUAL_UINT8(EL_DATE_EU, s.date_format);
    static const char *const T[] = { "1", "ON", "on", "True", "TRUE", "yes" };
    static const char *const F[] = { "0", "OFF", "Off", "false", "FALSE", "no" };
    for (size_t i = 0; i < 6; i++) {
        set_ok(&s, EL_SET_SERIAL_STREAM, T[i]); TEST_ASSERT_TRUE(s.serial_stream);
        set_ok(&s, EL_SET_SERIAL_STREAM, F[i]); TEST_ASSERT_FALSE(s.serial_stream);
    }
    *(uint8_t *)&s.serial_stream = 2;        /* corrupt in-memory byte: validate must notice */
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_validate(&s));
}

static void test_permissions_every_setting_every_level(void)
{
    for (int i = 0; i < EL_SET_COUNT; i++) {
        const el_set_def_t *d = el_settings_def((el_set_id_t)i);
        for (int lv = 0; lv < EL_LEVEL_COUNT; lv++) {
            el_settings_t s = defaults();
            char text[40];
            el_settings_get_str(&s, (el_set_id_t)i, text, sizeof text);
            if (d->secret)                            /* the value is masked: write a valid one */
                el_strlcpy(text, strcmp(d->key, "engineer_code") ? "" : "1234", sizeof text);
            el_err_t e = el_settings_set_str(&s, (el_set_id_t)i, text, (el_level_t)lv);
            char msg[80];
            snprintf(msg, sizeof msg, "%s level %d", d->key, lv);
            if (lv < (int)d->write_level) TEST_ASSERT_TRUE_MESSAGE(e == EL_SET_ERR_DENIED, msg);
            else TEST_ASSERT_EQUAL_MESSAGE(EL_OK, e, msg);
        }
    }
}

static void test_denied_is_distinct_and_checked_before_value(void)
{
    el_settings_t s = defaults();
    const el_settings_t before = s;
    TEST_ASSERT_TRUE(EL_SET_ERR_DENIED != EL_ERR_ARG && EL_SET_ERR_DENIED != EL_ERR_RANGE);
    /* an operator learns nothing about validity from a denial */
    TEST_ASSERT_TRUE(el_settings_set_str(&s, EL_SET_REPEATS, "999", EL_LEVEL_OPERATOR) == EL_SET_ERR_DENIED);
    TEST_ASSERT_TRUE(el_settings_set_str(&s, EL_SET_REPEATS, "5", EL_LEVEL_OPERATOR) == EL_SET_ERR_DENIED);
    TEST_ASSERT_TRUE(el_settings_set_str(&s, EL_SET_ADMIN_PIN, "1", EL_LEVEL_OPERATOR) == EL_SET_ERR_DENIED);
    TEST_ASSERT_TRUE(el_settings_set_str(&s, EL_SET_DEVICE_SERIAL, "X", EL_LEVEL_ADMIN) == EL_SET_ERR_DENIED);
    TEST_ASSERT_EQUAL_MEMORY(&before, &s, sizeof s);
    TEST_ASSERT_TRUE(el_settings_set_str(&s, EL_SET_ENGINEER_CODE, "9999", EL_LEVEL_ADMIN) == EL_SET_ERR_DENIED);
    TEST_ASSERT_TRUE(el_settings_set_str(&s, EL_SET_T_OFFSET, "1", EL_LEVEL_OPERATOR) == EL_SET_ERR_DENIED);
    TEST_ASSERT_EQUAL_MEMORY(&before, &s, sizeof s);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_set_str(&s, EL_SET_OPERATOR_ID, "JD", EL_LEVEL_OPERATOR));
    TEST_ASSERT_EQUAL_STRING("JD", s.operator_id);
}

static void test_cross_field_low_below_high(void)
{
    el_settings_t s = defaults();                       /* 5..35 degC, 20..80 %RH */
    const el_settings_t before = s;
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_set_str(&s, EL_SET_T_LOW, "35", EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_set_str(&s, EL_SET_T_LOW, "40", EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_set_str(&s, EL_SET_T_HIGH, "5", EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_set_str(&s, EL_SET_RH_LOW, "80", EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_set_str(&s, EL_SET_RH_HIGH, "10", EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL_MEMORY(&before, &s, sizeof s);          /* atomic: nothing changed */
    /* moving a freezer band below the current one needs low first */
    set_ok(&s, EL_SET_T_LOW, "-25");
    set_ok(&s, EL_SET_T_HIGH, "-15");
    /* and back up: high first */
    set_ok(&s, EL_SET_T_HIGH, "8");
    set_ok(&s, EL_SET_T_LOW, "2");
    TEST_ASSERT_EQUAL(EL_OK, el_settings_validate(&s));
}

static void test_cross_field_idle_off_after_dim(void)
{
    el_settings_t s = defaults();                       /* dim 120, off 600 */
    const el_settings_t before = s;
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_set_str(&s, EL_SET_IDLE_OFF, "120", EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_set_str(&s, EL_SET_IDLE_OFF, "60", EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_set_str(&s, EL_SET_IDLE_DIM, "600", EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_set_str(&s, EL_SET_IDLE_DIM, "3600", EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL_MEMORY(&before, &s, sizeof s);
    set_ok(&s, EL_SET_IDLE_OFF, "0");                   /* never off: dim can be anything */
    set_ok(&s, EL_SET_IDLE_DIM, "3600");
    set_ok(&s, EL_SET_IDLE_DIM, "0");
    set_ok(&s, EL_SET_IDLE_OFF, "30");
    set_ok(&s, EL_SET_IDLE_DIM, "29");
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_set_str(&s, EL_SET_IDLE_DIM, "30", EL_LEVEL_ADMIN));
}

static void test_validate_catches_every_corrupt_field(void)
{
    el_settings_t s = defaults();
    s.repeats = 0;                      TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_validate(&s));
    s = defaults(); s.temp_offset_c = NAN;
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_validate(&s));
    s = defaults(); s.log_interval_s = 5;                           /* 0 or 10..86400 */
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_validate(&s));
    s = defaults(); s.engineer_code[0] = '\0';                      /* at least 4 digits */
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_validate(&s));
    s = defaults(); memset(s.admin_pin, 'x', sizeof s.admin_pin);   /* unterminated + not digits */
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_validate(&s));
    s = defaults(); s.device_serial[0] = '\0';
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_validate(&s));
    s = defaults(); s.mb_baud = 5;
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_validate(&s));
    s = defaults(); s.rh_low_pct = 90;                              /* crosses high */
    TEST_ASSERT_EQUAL(EL_ERR_RANGE, el_settings_validate(&s));
}

static void test_secret_pin_is_masked_everywhere(void)
{
    el_settings_t s = defaults();
    char b[160];
    expect_get(&s, EL_SET_ADMIN_PIN, "****");          /* factory default PIN is set */
    set_ok(&s, EL_SET_ADMIN_PIN, "424242");
    expect_get(&s, EL_SET_ADMIN_PIN, "****");
    TEST_ASSERT_EQUAL(EL_OK, el_settings_describe_value(&s, EL_SET_ADMIN_PIN, b, sizeof b));
    TEST_ASSERT_NULL(strstr(b, "424242"));
    TEST_ASSERT_NOT_NULL(strstr(b, "value=****"));
    set_ok(&s, EL_SET_ADMIN_PIN, "");                 /* clearing the PIN is allowed */
    expect_get(&s, EL_SET_ADMIN_PIN, "");
    TEST_ASSERT_EQUAL(EL_OK, el_settings_describe_value(&s, EL_SET_ADMIN_PIN, b, sizeof b));
    TEST_ASSERT_NOT_NULL(strstr(b, "value="));
}

static void test_describe_line_format(void)
{
    char b[200];
    el_settings_t s = defaults();
    TEST_ASSERT_EQUAL(EL_OK, el_settings_describe(EL_SET_MB_BAUD, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("key=mb_baud type=enum range=9600|19200|38400|57600|115200 level=ADMIN unit=- default=19200", b);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_describe_value(&s, EL_SET_MB_BAUD, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("key=mb_baud type=enum range=9600|19200|38400|57600|115200 level=ADMIN unit=- default=19200 value=19200", b);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_describe_value(&s, EL_SET_T_HIGH, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("key=temp_high_c type=float range=-40..125 level=ADMIN unit=C default=35 value=35", b);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_describe(EL_SET_LOG_INTERVAL, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("key=log_interval_s type=int range=0..86400 level=ADMIN unit=s default=300", b);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_describe(EL_SET_SERIAL_STREAM, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("key=serial_stream type=bool range=0|1 level=ADMIN unit=- default=0", b);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_describe(EL_SET_OPERATOR_ID, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("key=operator_id type=string range=0..7 level=OPERATOR unit=- default=", b);
    for (int i = 0; i < EL_SET_COUNT; i++) {          /* every line fits and starts with key= */
        TEST_ASSERT_EQUAL(EL_OK, el_settings_describe_value(&s, (el_set_id_t)i, b, sizeof b));
        TEST_ASSERT_EQUAL(0, strncmp(b, "key=", 4));
    }
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_settings_describe(EL_SET_MB_BAUD, b, 20));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_settings_describe(EL_SET_COUNT, b, sizeof b));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_settings_describe_value(NULL, EL_SET_MB_BAUD, b, sizeof b));
}

static el_settings_t customised(void)
{
    el_settings_t s = defaults();
    set_ok(&s, EL_SET_DEVICE_SERIAL, "EL-0042");
    set_ok(&s, EL_SET_REPEATS, "7");  set_ok(&s, EL_SET_DECIMALS, "2");
    set_ok(&s, EL_SET_LOG_INTERVAL, "900"); set_ok(&s, EL_SET_LOCATION, "Archive B");
    set_ok(&s, EL_SET_T_LOW, "15"); set_ok(&s, EL_SET_T_HIGH, "25");
    set_ok(&s, EL_SET_RH_LOW, "35"); set_ok(&s, EL_SET_RH_HIGH, "55");
    set_ok(&s, EL_SET_T_OFFSET, "-0.3"); set_ok(&s, EL_SET_RH_OFFSET, "1.5");
    set_ok(&s, EL_SET_SENSOR_TYPE, "SHT3X"); set_ok(&s, EL_SET_BRIGHTNESS, "35");
    set_ok(&s, EL_SET_IDLE_DIM, "0"); set_ok(&s, EL_SET_IDLE_OFF, "900");
    set_ok(&s, EL_SET_DATE_FORMAT, "EU"); set_ok(&s, EL_SET_UTC_OFFSET, "-420");
    set_ok(&s, EL_SET_MB_ADDR, "99"); set_ok(&s, EL_SET_MB_BAUD, "38400");
    set_ok(&s, EL_SET_MB_PARITY, "NONE"); set_ok(&s, EL_SET_SERIAL_STREAM, "1");
    set_ok(&s, EL_SET_DO1_MODE, "BUSY"); set_ok(&s, EL_SET_DI0_TRIGGER, "1");
    set_ok(&s, EL_SET_ADMIN_PIN, "2468"); set_ok(&s, EL_SET_LOGIN_IDLE, "120");
    set_ok(&s, EL_SET_ENGINEER_CODE, "13579"); set_ok(&s, EL_SET_OPERATOR_ID, "JD");
    return s;
}

static void test_encode_decode_round_trip(void)
{
    uint8_t b[EL_SETTINGS_ENCODED_MAX];
    size_t n = 0;
    el_settings_t in = customised(), out;
    el_settings_report_t rep;
    TEST_ASSERT_EQUAL(EL_OK, el_settings_encode(&in, b, sizeof b, &n));
    TEST_ASSERT_TRUE(n > 10 && n <= EL_SETTINGS_ENCODED_MAX);
    TEST_ASSERT_EQUAL_HEX16(EL_SETTINGS_MAGIC, el_get_u16le(b));
    TEST_ASSERT_EQUAL(EL_OK, el_settings_decode_ex(b, n, &out, &rep));
    TEST_ASSERT_EQUAL_UINT8(0, rep.unknown); TEST_ASSERT_EQUAL_UINT8(0, rep.missing);
    TEST_ASSERT_EQUAL_UINT8(0, rep.defaulted);
    for (int i = 0; i < EL_SET_COUNT; i++) {
        char a[64], c[64];
        el_settings_describe_value(&in, (el_set_id_t)i, a, sizeof a);
        el_settings_describe_value(&out, (el_set_id_t)i, c, sizeof c);
        TEST_ASSERT_EQUAL_STRING(a, c);
    }
    TEST_ASSERT_EQUAL_STRING("2468", out.admin_pin);       /* PIN is persisted, just never printed */
    TEST_ASSERT_EQUAL_STRING("13579", out.engineer_code);
    TEST_ASSERT_EQUAL_FLOAT(-0.3f, out.temp_offset_c);
    TEST_ASSERT_EQUAL_UINT32(900, out.log_interval_s);
    TEST_ASSERT_EQUAL_INT16(-420, out.utc_offset_min);
    /* trailing bytes (erased flash padding) are ignored */
    uint8_t padded[EL_SETTINGS_ENCODED_MAX + 16];
    memset(padded, 0xFF, sizeof padded);
    memcpy(padded, b, n);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_decode(padded, sizeof padded, &out));
    TEST_ASSERT_EQUAL_UINT8(7, out.repeats);
}

static void test_worst_case_size_fits_encoded_max(void)
{
    el_settings_t s = defaults();
    uint8_t b[EL_SETTINGS_ENCODED_MAX];
    size_t n;
    set_ok(&s, EL_SET_DEVICE_SERIAL, "123456789012345"); set_ok(&s, EL_SET_LOCATION, "1234567890123456789");
    set_ok(&s, EL_SET_ENGINEER_CODE, "12345678");
    set_ok(&s, EL_SET_WIFI_SSID, "12345678901234567890123456789012");
    set_ok(&s, EL_SET_WIFI_PASSWORD, "123456789012345678901234567890123456789012345678901234567890123");
    set_ok(&s, EL_SET_UPLOAD_URL, "https://a.example/123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789");
    set_ok(&s, EL_SET_UPLOAD_KEY, "1234567890123456789012345678901234567890123456789012345678901234");
    set_ok(&s, EL_SET_ADMIN_PIN, "12345678"); set_ok(&s, EL_SET_OPERATOR_ID, "1234567");
    TEST_ASSERT_EQUAL(EL_OK, el_settings_encode(&s, b, sizeof b, &n));
    TEST_ASSERT_TRUE(n <= EL_SETTINGS_ENCODED_MAX);
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_settings_encode(&s, b, n - 1, &n));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_settings_encode(NULL, b, sizeof b, &n));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_settings_encode(&s, NULL, sizeof b, &n));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_settings_encode(&s, b, sizeof b, NULL));
}

static void test_unknown_tag_from_newer_firmware_is_skipped(void)
{
    uint8_t b[EL_SETTINGS_ENCODED_MAX + 16];
    size_t n;
    el_settings_t in = customised(), out;
    el_settings_report_t rep;
    el_settings_encode(&in, b, EL_SETTINGS_ENCODED_MAX, &n);
    /* append {tag 0x0777, len 5, "hello"} to the payload, fix the length, re-sign */
    size_t end = n - 4;
    el_put_u16le(b + end, 0x0777); b[end + 2] = 5; memcpy(b + end + 3, "hello", 5);
    n += 8;
    el_put_u16le(b + 4, (uint16_t)(el_get_u16le(b + 4) + 8));
    resign(b, n);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_decode_ex(b, n, &out, &rep));
    TEST_ASSERT_EQUAL_UINT8(1, rep.unknown);
    TEST_ASSERT_EQUAL_UINT8(0, rep.defaulted);
    TEST_ASSERT_EQUAL_UINT8(7, out.repeats);
    TEST_ASSERT_EQUAL_STRING("EL-0042", out.device_serial);
    TEST_ASSERT_EQUAL_STRING("2468", out.admin_pin);
}

static void test_missing_tag_keeps_default(void)
{
    /* an "older firmware" encoding that knows only `repeats` (tag 10) */
    uint8_t c[17];
    el_put_u16le(c, EL_SETTINGS_MAGIC); c[2] = EL_SETTINGS_VERSION; c[3] = 0;
    el_put_u16le(c + 4, 7);
    el_put_u16le(c + 6, 10); c[8] = 4; el_put_u32le(c + 9, 8);
    resign(c, sizeof c);
    el_settings_t out;
    el_settings_report_t rep;
    TEST_ASSERT_EQUAL(EL_OK, el_settings_decode_ex(c, sizeof c, &out, &rep));
    TEST_ASSERT_EQUAL_UINT8(8, out.repeats);
    TEST_ASSERT_EQUAL_UINT8(EL_SET_COUNT - 1, rep.missing);
    el_settings_t d = defaults();
    d.repeats = 8;
    TEST_ASSERT_EQUAL_MEMORY(&d, &out, sizeof d);
}

static void test_stored_out_of_range_value_falls_back_to_default(void)
{
    uint8_t b[EL_SETTINGS_ENCODED_MAX];
    size_t n;
    el_settings_t in = customised(), out;
    el_settings_report_t rep;
    el_settings_encode(&in, b, sizeof b, &n);
    size_t p = find_tlv(b, n, 10);                       /* repeats */
    TEST_ASSERT_TRUE(p != 0);
    el_put_u32le(b + p + 3, 99);
    p = find_tlv(b, n, 31);                              /* mb_baud enum index 9 */
    b[p + 3] = 9;
    p = find_tlv(b, n, 40);                              /* admin_pin: letters */
    memcpy(b + p + 3, "ab12", 4);
    resign(b, n);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_decode_ex(b, n, &out, &rep));
    TEST_ASSERT_EQUAL_UINT8(3, rep.defaulted);
    TEST_ASSERT_EQUAL_UINT8(3, out.repeats);             /* default */
    TEST_ASSERT_EQUAL_UINT8(EL_BAUD_19200, out.mb_baud);
    TEST_ASSERT_EQUAL_STRING(EL_DEFAULT_PIN, out.admin_pin);     /* default */
    TEST_ASSERT_EQUAL_UINT8(99, out.mb_addr);            /* neighbours untouched */
    TEST_ASSERT_EQUAL(EL_OK, el_settings_validate(&out));
}

static void test_stored_nan_and_wrong_length_fall_back(void)
{
    uint8_t b[EL_SETTINGS_ENCODED_MAX];
    size_t n;
    el_settings_t in = customised(), out;
    el_settings_report_t rep;
    el_settings_encode(&in, b, sizeof b, &n);
    size_t p = find_tlv(b, n, 18);                       /* temperature offset -> NaN */
    el_put_u32le(b + p + 3, 0x7FC00000u);
    p = find_tlv(b, n, 5);                               /* sensor type (enum) stored with a 2-byte value */
    memmove(b + p + 5, b + p + 4, n - p - 4);            /* make room: one extra value byte */
    b[p + 2] = 2;
    n++;
    el_put_u16le(b + 4, (uint16_t)(el_get_u16le(b + 4) + 1));
    resign(b, n);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_decode_ex(b, n, &out, &rep));
    TEST_ASSERT_EQUAL_UINT8(2, rep.defaulted);           /* NaN offset + mis-sized sensor type */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.temp_offset_c);
    TEST_ASSERT_EQUAL_UINT8(EL_SENSOR_SHT4X, out.sensor_type);
    TEST_ASSERT_EQUAL_UINT8(7, out.repeats);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_validate(&out));
}

static void test_stored_cross_field_conflict_falls_back(void)
{
    uint8_t b[EL_SETTINGS_ENCODED_MAX];
    size_t n;
    el_settings_t in = customised(), out;
    el_settings_report_t rep;
    el_settings_encode(&in, b, sizeof b, &n);
    size_t p = find_tlv(b, n, 14);                       /* T low 15 -> 30 > T high 25 */
    el_put_f32le(b + p + 3, 30.0f);
    resign(b, n);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_decode_ex(b, n, &out, &rep));
    TEST_ASSERT_EQUAL_UINT8(1, rep.defaulted);
    TEST_ASSERT_EQUAL(EL_OK, el_settings_validate(&out));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, out.temp_low_c);       /* the cross-checked group is reset */
    TEST_ASSERT_EQUAL_FLOAT(35.0f, out.temp_high_c);
    TEST_ASSERT_EQUAL_UINT8(7, out.repeats);
    TEST_ASSERT_EQUAL_STRING("Archive B", out.location);
}

static void test_every_byte_corruption_is_detected(void)
{
    uint8_t good[EL_SETTINGS_ENCODED_MAX], b[EL_SETTINGS_ENCODED_MAX];
    size_t n;
    el_settings_t in = customised(), out, def = defaults();
    el_settings_encode(&in, good, sizeof good, &n);
    for (size_t i = 0; i < n; i++) {
        for (int bit = 0; bit < 8; bit += 7) {           /* lowest and highest bit */
            memcpy(b, good, n);
            b[i] = (uint8_t)(b[i] ^ (1u << bit));
            el_settings_defaults(&out); out.repeats = 9; /* poison: decode must overwrite */
            char msg[40];
            snprintf(msg, sizeof msg, "byte %u bit %d", (unsigned)i, bit);
            TEST_ASSERT_TRUE_MESSAGE(el_settings_decode(b, n, &out) != EL_OK, msg);
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&def, &out, sizeof def, msg);
        }
    }
    memcpy(b, good, n);
    b[n - 1] ^= 0xFF;
    TEST_ASSERT_EQUAL(EL_ERR_CRC, el_settings_decode(b, n, &out));
}

static void test_truncation_at_every_length_is_safe(void)
{
    uint8_t good[EL_SETTINGS_ENCODED_MAX];
    size_t n;
    el_settings_t in = customised(), out, def = defaults();
    el_settings_encode(&in, good, sizeof good, &n);
    for (size_t len = 0; len < n; len++) {               /* a power cut mid-write */
        uint8_t *exact = malloc(len ? len : 1);          /* exact size: overreads show up in tools */
        TEST_ASSERT_NOT_NULL(exact);
        memcpy(exact, good, len);
        out = in;
        TEST_ASSERT_TRUE(el_settings_decode(exact, len, &out) != EL_OK);
        TEST_ASSERT_EQUAL_MEMORY(&def, &out, sizeof def);
        free(exact);
    }
    TEST_ASSERT_EQUAL(EL_OK, el_settings_decode(good, n, &out));
}

static void test_blank_flash_and_foreign_data(void)
{
    uint8_t ff[64], zero[64];
    el_settings_t out;
    memset(ff, 0xFF, sizeof ff); memset(zero, 0, sizeof zero);
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_settings_decode(ff, sizeof ff, &out));
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_settings_decode(zero, sizeof zero, &out));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_settings_decode(NULL, 10, &out));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_settings_decode(ff, sizeof ff, NULL));
}

static void test_newer_format_version_is_reported(void)
{
    uint8_t b[EL_SETTINGS_ENCODED_MAX];
    size_t n;
    el_settings_t in = defaults(), out;
    el_settings_encode(&in, b, sizeof b, &n);
    b[2] = EL_SETTINGS_VERSION + 1;
    resign(b, n);
    TEST_ASSERT_EQUAL(EL_ERR_VERSION, el_settings_decode(b, n, &out));
    b[2] = 0;
    resign(b, n);
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_settings_decode(b, n, &out));
}

static void test_malformed_tlv_with_valid_crc_is_format_error(void)
{
    uint8_t b[EL_SETTINGS_ENCODED_MAX];
    size_t n;
    el_settings_t in = defaults(), out;
    el_settings_encode(&in, b, sizeof b, &n);
    b[6 + 2] = 200;                                      /* first TLV claims 200 bytes */
    resign(b, n);
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_settings_decode(b, n, &out));
    TEST_ASSERT_EQUAL_UINT8(3, out.repeats);
}

void run_settings_tests(void)
{
    RUN_TEST(test_defaults_match_spec_and_validate);
    RUN_TEST(test_schema_is_consistent);
    RUN_TEST(test_find_is_case_insensitive);
    RUN_TEST(test_set_get_round_trip_every_setting);
    RUN_TEST(test_boundaries_accepted_and_just_outside_rejected);
    RUN_TEST(test_bad_values_rejected_and_struct_unchanged);
    RUN_TEST(test_argument_errors);
    RUN_TEST(test_enum_and_bool_parsing);
    RUN_TEST(test_permissions_every_setting_every_level);
    RUN_TEST(test_denied_is_distinct_and_checked_before_value);
    RUN_TEST(test_cross_field_low_below_high);
    RUN_TEST(test_cross_field_idle_off_after_dim);
    RUN_TEST(test_validate_catches_every_corrupt_field);
    RUN_TEST(test_secret_pin_is_masked_everywhere);
    RUN_TEST(test_describe_line_format);
    RUN_TEST(test_encode_decode_round_trip);
    RUN_TEST(test_worst_case_size_fits_encoded_max);
    RUN_TEST(test_unknown_tag_from_newer_firmware_is_skipped);
    RUN_TEST(test_missing_tag_keeps_default);
    RUN_TEST(test_stored_out_of_range_value_falls_back_to_default);
    RUN_TEST(test_stored_nan_and_wrong_length_fall_back);
    RUN_TEST(test_stored_cross_field_conflict_falls_back);
    RUN_TEST(test_every_byte_corruption_is_detected);
    RUN_TEST(test_truncation_at_every_length_is_safe);
    RUN_TEST(test_blank_flash_and_foreign_data);
    RUN_TEST(test_newer_format_version_is_reported);
    RUN_TEST(test_malformed_tlv_with_valid_crc_is_format_error);
}
