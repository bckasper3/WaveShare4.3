#include <math.h>
#include <string.h>
#include "unity.h"
#include "el_app_modbus.h"

static el_app_mb_src_t src_with_result(void)
{
    el_app_mb_src_t s; memset(&s, 0, sizeof s);
    s.has_result = true;
    s.last.temp_c = 23.5f; s.last.rh_pct = 61.0f; s.last.dew_point_c = 15.6f;
    s.last.temp_sd_c = 0.02f; s.last.rh_sd_pct = 0.1f;
    s.last.status = EL_RES_ALARM; s.last.alarms = EL_ALARM_RH_HIGH; s.last.seq = 70042;
    s.last.timestamp_s = 1793534400; s.last.repeats = 3;
    strcpy(s.last.tag, "Archive room");
    s.result_counter = 70042; s.meas_state = 1; s.progress_pct = 40;
    s.simulated = true; s.sensor_fault = true; s.sd_present = true;
    s.next_auto_s = 120;
    s.uptime_s = 3600; s.log_count = 1234; s.log_fill_pct = 3;
    s.fw_version = "0.1.0-dev"; s.serial = "EL-0001";
    return s;
}

static void test_inputs_mapping(void)
{
    el_app_mb_src_t s = src_with_result();
    el_mb_inputs_t in;
    el_app_mb_inputs(&s, &in);
    TEST_ASSERT_EQUAL_FLOAT(23.5f, in.temp_c);
    TEST_ASSERT_EQUAL_FLOAT(61.0f, in.rh_pct);
    TEST_ASSERT_EQUAL_FLOAT(15.6f, in.dew_point_c);
    TEST_ASSERT_EQUAL_FLOAT(0.02f, in.temp_sd_c);
    TEST_ASSERT_EQUAL_FLOAT(0.1f, in.rh_sd_pct);
    TEST_ASSERT_EQUAL_UINT16(2, in.status);
    TEST_ASSERT_EQUAL_UINT16(EL_ALARM_RH_HIGH, in.alarms);
    TEST_ASSERT_EQUAL_UINT16(70042u & 0xFFFFu, in.result_counter);
    TEST_ASSERT_EQUAL_UINT32(70042, in.last_seq);
    TEST_ASSERT_EQUAL_UINT32(1793534400u, in.last_timestamp_s);
    TEST_ASSERT_EQUAL_UINT16(3, in.repeats);
    TEST_ASSERT_EQUAL_UINT16(1, in.meas_state);
    TEST_ASSERT_EQUAL_UINT16(40, in.progress_pct);
    TEST_ASSERT_EQUAL_HEX16(EL_MB_FLAG_SENSOR_FAULT | EL_MB_FLAG_SIMULATED | EL_MB_FLAG_AUTOLOG_ON |
                            EL_MB_FLAG_SD_PRESENT, in.flags);
    TEST_ASSERT_EQUAL_UINT32(120, in.next_auto_s);
    TEST_ASSERT_EQUAL_UINT16(0, in.fw_major);
    TEST_ASSERT_EQUAL_UINT16(1, in.fw_minor);
    TEST_ASSERT_EQUAL_UINT16(0, in.fw_patch);
    TEST_ASSERT_EQUAL_STRING("EL-0001", in.serial);
    TEST_ASSERT_EQUAL_STRING("Archive room", in.last_tag);
    TEST_ASSERT_EQUAL_UINT32(3600, in.uptime_s);
    TEST_ASSERT_EQUAL_UINT32(1234, in.log_count);
    TEST_ASSERT_EQUAL_UINT16(3, in.log_used_pct);
}

static void test_inputs_without_result(void)
{
    el_app_mb_src_t s; memset(&s, 0, sizeof s);
    s.log_near_full = true; s.export_busy = true; s.next_auto_s = UINT32_MAX;
    el_mb_inputs_t in;
    el_app_mb_inputs(&s, &in);
    TEST_ASSERT_TRUE(isnan(in.temp_c));
    TEST_ASSERT_TRUE(isnan(in.rh_pct));
    TEST_ASSERT_TRUE(isnan(in.dew_point_c));
    TEST_ASSERT_EQUAL_UINT16(0, in.status);
    TEST_ASSERT_EQUAL_HEX16(EL_MB_FLAG_LOG_NEAR_FULL | EL_MB_FLAG_EXPORT_BUSY, in.flags);   /* autolog off */
    TEST_ASSERT_EQUAL_HEX32(UINT32_MAX, in.next_auto_s);
    TEST_ASSERT_EQUAL_STRING("", in.serial);
    s.has_result = true; s.last.timestamp_s = -5;
    el_app_mb_inputs(&s, &in);
    TEST_ASSERT_EQUAL_UINT32(0, in.last_timestamp_s);
}

static void test_holding_from_settings(void)
{
    el_settings_t s; el_settings_defaults(&s);
    s.repeats = 4; s.temp_low_c = 2.0f; s.temp_high_c = 8.0f; s.rh_low_pct = 30.0f; s.rh_high_pct = 60.0f;
    s.mb_addr = 17; s.mb_baud = EL_BAUD_115200; s.mb_parity = EL_PARITY_NONE; s.log_interval_s = 900;
    el_mb_holding_t h;
    el_app_mb_holding(&s, &h);
    TEST_ASSERT_EQUAL_UINT16(4, h.repeats);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, h.limits.t_low_c);
    TEST_ASSERT_EQUAL_FLOAT(8.0f, h.limits.t_high_c);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, h.limits.rh_low_pct);
    TEST_ASSERT_EQUAL_FLOAT(60.0f, h.limits.rh_high_pct);
    TEST_ASSERT_EQUAL_UINT16(17, h.address);
    TEST_ASSERT_EQUAL_UINT16(4, h.baud_code);
    TEST_ASSERT_EQUAL_UINT16(0, h.parity_code);
    TEST_ASSERT_EQUAL_UINT32(900, h.log_interval_s);
}

static void test_gate(void)
{
    el_mb_effects_t fx; memset(&fx, 0, sizeof fx);
    TEST_ASSERT_EQUAL(EL_MB_ACCEPT, el_app_mb_gate(&fx, true, true));   /* settings only */
    fx.mask = EL_MB_FX_COMMAND; fx.command = EL_MB_CMD_START;
    TEST_ASSERT_EQUAL(EL_MB_ACCEPT, el_app_mb_gate(&fx, false, false));
    TEST_ASSERT_EQUAL(EL_MB_REJECT_BUSY, el_app_mb_gate(&fx, true, false));
    TEST_ASSERT_EQUAL(EL_MB_REJECT_BUSY, el_app_mb_gate(&fx, false, true));
    fx.command = EL_MB_CMD_CANCEL;
    TEST_ASSERT_EQUAL(EL_MB_ACCEPT, el_app_mb_gate(&fx, true, false));
    fx.command = EL_MB_CMD_CLEAR_ALARM;
    TEST_ASSERT_EQUAL(EL_MB_ACCEPT, el_app_mb_gate(&fx, true, true));
    TEST_ASSERT_EQUAL(EL_MB_ACCEPT, el_app_mb_gate(NULL, true, true));
}

static void test_pair_order_always_has_valid_intermediate(void)
{
    static const float V[] = { -40.0f, -5.0f, 0.0f, 8.0f, 25.0f, 60.0f, 125.0f };
    const int n = (int)(sizeof V / sizeof V[0]);
    for (int a = 0; a < n; a++) for (int b = 0; b < n; b++)
    for (int c = 0; c < n; c++) for (int d = 0; d < n; d++) {
        float ol = V[a], oh = V[b], nl = V[c], nh = V[d];
        if (!(ol < oh) || !(nl < nh)) continue;
        bool hf = el_app_pair_high_first(ol, oh, nl, nh);
        TEST_ASSERT_TRUE(hf ? ol < nh : nl < oh);
    }
}

static void test_plan_orders_and_formats(void)
{
    el_settings_t s; el_settings_defaults(&s);
    s.temp_low_c = 5.0f; s.temp_high_c = 35.0f;
    el_mb_effects_t fx; memset(&fx, 0, sizeof fx);
    /* move the temperature band above the old high limit: high must go first */
    fx.mask = EL_MB_FX_REPEATS | EL_MB_FX_T_LOW | EL_MB_FX_T_HIGH | EL_MB_FX_BAUD | EL_MB_FX_PARITY |
              EL_MB_FX_ADDRESS | EL_MB_FX_COMMAND | EL_MB_FX_INTERVAL;
    fx.set.repeats = 5; fx.set.limits.t_low_c = 40.0f; fx.set.limits.t_high_c = 60.5f;
    fx.set.address = 12; fx.set.baud_code = 4; fx.set.parity_code = 0; fx.set.log_interval_s = 60;
    el_app_set_op_t ops[10];
    size_t n = el_app_mb_plan(&fx, &s, ops, 10);
    TEST_ASSERT_EQUAL_size_t(7, n);
    TEST_ASSERT_EQUAL(EL_SET_REPEATS, ops[0].id);   TEST_ASSERT_EQUAL_STRING("5", ops[0].text);
    TEST_ASSERT_EQUAL(EL_SET_T_HIGH, ops[1].id);    TEST_ASSERT_EQUAL_STRING("60.5", ops[1].text);
    TEST_ASSERT_EQUAL(EL_SET_T_LOW, ops[2].id);     TEST_ASSERT_EQUAL_STRING("40", ops[2].text);
    TEST_ASSERT_EQUAL(EL_SET_MB_ADDR, ops[3].id);   TEST_ASSERT_EQUAL_STRING("12", ops[3].text);
    TEST_ASSERT_EQUAL(EL_SET_MB_BAUD, ops[4].id);
    TEST_ASSERT_EQUAL(EL_SET_MB_PARITY, ops[5].id);
    TEST_ASSERT_EQUAL(EL_SET_LOG_INTERVAL, ops[6].id); TEST_ASSERT_EQUAL_STRING("60", ops[6].text);
    /* The texts must be accepted by the settings parser, applied in this order. */
    for (size_t i = 0; i < n; i++)
        TEST_ASSERT_EQUAL(EL_OK, el_settings_set_str(&s, ops[i].id, ops[i].text, EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL_FLOAT(40.0f, s.temp_low_c);
    TEST_ASSERT_EQUAL_FLOAT(60.5f, s.temp_high_c);
    TEST_ASSERT_EQUAL_UINT8(EL_BAUD_115200, s.mb_baud);
    TEST_ASSERT_EQUAL_UINT8(EL_PARITY_NONE, s.mb_parity);
    TEST_ASSERT_EQUAL_UINT8(12, s.mb_addr);
    TEST_ASSERT_EQUAL_UINT32(60, s.log_interval_s);
    /* humidity band moved below the old low: low first */
    s.rh_low_pct = 40.0f; s.rh_high_pct = 60.0f;
    fx.mask = EL_MB_FX_RH_LOW | EL_MB_FX_RH_HIGH;
    fx.set.limits.rh_low_pct = 10.0f; fx.set.limits.rh_high_pct = 20.0f;
    n = el_app_mb_plan(&fx, &s, ops, 10);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL(EL_SET_RH_LOW, ops[0].id);
    for (size_t i = 0; i < n; i++)
        TEST_ASSERT_EQUAL(EL_OK, el_settings_set_str(&s, ops[i].id, ops[i].text, EL_LEVEL_ADMIN));
    /* cap respected, bad enum code skipped */
    fx.mask = EL_MB_FX_RH_LOW | EL_MB_FX_RH_HIGH | EL_MB_FX_BAUD;
    fx.set.baud_code = 9;
    TEST_ASSERT_EQUAL_size_t(1, el_app_mb_plan(&fx, &s, ops, 1));
    TEST_ASSERT_EQUAL_size_t(2, el_app_mb_plan(&fx, &s, ops, 10));
    TEST_ASSERT_EQUAL_size_t(0, el_app_mb_plan(NULL, &s, ops, 10));
}

static void test_rtu_timing(void)
{
    TEST_ASSERT_EQUAL_UINT32(1146, el_app_mb_char_us(9600));
    TEST_ASSERT_EQUAL_UINT32(573, el_app_mb_char_us(19200));
    TEST_ASSERT_EQUAL_UINT32(2006, el_app_mb_t35_us(19200));
    TEST_ASSERT_EQUAL_UINT32(1750, el_app_mb_t35_us(38400));
    TEST_ASSERT_EQUAL_UINT32(1750, el_app_mb_t35_us(115200));
    TEST_ASSERT_EQUAL_UINT8(3, el_app_mb_rx_tout_chars(9600));
    TEST_ASSERT_EQUAL_UINT8(3, el_app_mb_rx_tout_chars(19200));
    TEST_ASSERT_EQUAL_UINT8(6, el_app_mb_rx_tout_chars(38400));
    TEST_ASSERT_EQUAL_UINT8(9, el_app_mb_rx_tout_chars(57600));
    TEST_ASSERT_EQUAL_UINT8(18, el_app_mb_rx_tout_chars(115200));
    TEST_ASSERT_EQUAL_UINT8(126, el_app_mb_rx_tout_chars(4000000));  /* clamp */
    TEST_ASSERT_EQUAL_UINT32(0, el_app_mb_char_us(0));
    TEST_ASSERT_EQUAL_UINT8(3, el_app_mb_rx_tout_chars(0));
}

static void test_version_parse(void)
{
    uint16_t a, b, c;
    TEST_ASSERT_EQUAL(EL_OK, el_app_parse_version("1.12.3", &a, &b, &c));
    TEST_ASSERT_EQUAL_UINT16(1, a); TEST_ASSERT_EQUAL_UINT16(12, b); TEST_ASSERT_EQUAL_UINT16(3, c);
    TEST_ASSERT_EQUAL(EL_OK, el_app_parse_version("2.0", &a, &b, &c));
    TEST_ASSERT_EQUAL_UINT16(2, a); TEST_ASSERT_EQUAL_UINT16(0, b); TEST_ASSERT_EQUAL_UINT16(0, c);
    TEST_ASSERT_EQUAL(EL_OK, el_app_parse_version("3.4.5-rc1.7", &a, &b, &c));
    TEST_ASSERT_EQUAL_UINT16(5, c);
    TEST_ASSERT_EQUAL(EL_OK, el_app_parse_version("99999.1.1", &a, &b, &c));
    TEST_ASSERT_EQUAL_UINT16(65535, a);
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_app_parse_version("v1.2.3", &a, &b, &c));
    TEST_ASSERT_EQUAL_UINT16(0, a);
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_app_parse_version(NULL, &a, &b, &c));
}

void run_app_modbus_tests(void)
{
    RUN_TEST(test_inputs_mapping);
    RUN_TEST(test_inputs_without_result);
    RUN_TEST(test_holding_from_settings);
    RUN_TEST(test_gate);
    RUN_TEST(test_pair_order_always_has_valid_intermediate);
    RUN_TEST(test_plan_orders_and_formats);
    RUN_TEST(test_rtu_timing);
    RUN_TEST(test_version_parse);
}
