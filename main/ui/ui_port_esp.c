/* ui_port.h on the device (left out of the PC simulator by the _esp.c suffix). */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "ui_port.h"
#include "net.h"
#include "app_config.h"
#include "app_events.h"
#include "app_selftest.h"
#include "app_state.h"
#include "display.h"
#include "fw_update.h"
#include "i2c_bus.h"
#include "rtc.h"
#include "sensor.h"
#include "storage.h"

bool ui_port_lock(uint32_t timeout_ms) { return display_lock(timeout_ms); }
void ui_port_unlock(void) { display_unlock(); }
uint32_t ui_port_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
int64_t ui_port_now(void) { return (int64_t)time(NULL); }

void ui_port_backlight(uint8_t pct)
{
    static int last = -1;                    /* each change is a CH422G write: only on change */
    int on = pct > 0;
    if (on == last) return;
    last = on;
    display_set_brightness(pct);
}

/* AP3032 CTRL is the CH422G DISP line: on/off only (docs/HARDWARE.md section 8). */
bool ui_port_backlight_dimmable(void) { return false; }


el_err_t ui_port_set_time(int64_t utc)
{
    if (rtc_set((time_t)utc) != ESP_OK) return EL_ERR_RANGE;
    app_state_lock();
    app_state_raw()->rtc_valid = true;
    app_state_unlock();
    storage_event_log("clock set from the touchscreen");
    app_events_post(EVT_TIME_SET, SRC_UI, 0);
    return EL_OK;
}

bool ui_port_engineer_login_enabled(void) { return true; }   /* code: setting engineer_code */

const char *ui_port_fw_version(void) { return fw_update_running_version(); }
const char *ui_port_board_rev(void) { return "Waveshare 4.3B"; }
bool ui_port_fw_file(char *ver, size_t cap)
{
    static uint32_t last_ms;
    static bool cached;
    static char cver[24];
    uint32_t now = ui_port_ms();
    if (last_ms == 0 || now - last_ms > 3000) {   /* a file stat on the drive: not every 500 ms */
        last_ms = now;
        cver[0] = '\0';
        cached = storage_sd_present() && fw_update_file_present(cver, sizeof cver);
    }
    if (ver && cap) snprintf(ver, cap, "%s", cver);
    return cached;
}

void ui_port_sensor_info(char *buf, size_t cap)
{
    if (sensor_get_info(buf, cap) != ESP_OK) snprintf(buf, cap, "%s", sensor_simulated() ? "Simulated" : "Unknown");
}

void ui_port_diag(ui_diag_t *d)
{
    memset(d, 0, sizeof *d);
    d->heap_free = esp_get_free_heap_size();
    d->heap_min = esp_get_minimum_free_heap_size();
    d->psram_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    storage_internal_space(&d->data_free, &d->data_total);
    if (storage_sd_present()) storage_sd_space(&d->sd_free, &d->sd_total);
    storage_sd_status_t sd;
    storage_sd_status(&sd);
    d->sd_mirror_behind = sd.mirror_behind;
    d->sd_mirror_errors = sd.mirror_errors;
    d->events_dropped = sd.events_dropped;
    d->events_lost = app_events_dropped();

    /* Known bus devices, re-probed at most every 10 s (each probe takes the bus mutex). */
    static uint32_t last_ms;
    static char text[64];
    uint32_t now = ui_port_ms();
    if (last_ms == 0 || now - last_ms > 10000) {
        static const uint8_t ADDR[] = { 0x24, 0x51, 0x5D, 0x44 };
        static const char *const NAME[] = { "IO", "RTC", "TP", "SHT" };
        size_t n = 0;
        text[0] = '\0';
        for (size_t i = 0; i < sizeof ADDR; i++) {
            bool ok = i2c_bus_probe(ADDR[i]) == ESP_OK;
            n += (size_t)snprintf(text + n, sizeof text - n, "%s%s%s", n ? " " : "", NAME[i], ok ? "" : "!");
        }
        last_ms = now;
    }
    snprintf(d->i2c_text, sizeof d->i2c_text, "%s", text);
}

/* ---- background self-test ---- */
static volatile bool s_st_running;
static ui_selftest_status_t s_st;

static void st_sink(void *user, const el_test_def_t *t, const el_test_report_t *r)
{
    (void)user;
    if (r->outcome == EL_TEST_FAIL && !s_st.first_fail[0]) snprintf(s_st.first_fail, sizeof s_st.first_fail, "%s", t->id);
}

static void st_task(void *arg)
{
    (void)arg;
    el_test_summary_t sum = { 0 };
    app_selftest_run_group(EL_TG_BOOT, st_sink, NULL, &sum);
    s_st.pass = sum.count[EL_TEST_PASS];
    s_st.warn = sum.count[EL_TEST_WARN];
    s_st.fail = sum.count[EL_TEST_FAIL];
    s_st.skip = sum.count[EL_TEST_SKIP];
    s_st.done = true;
    s_st_running = false;
    storage_event_log("UI self-test: pass=%u warn=%u fail=%u", s_st.pass, s_st.warn, s_st.fail);
    vTaskDelete(NULL);
}

bool ui_port_selftest_start(void)
{
    if (s_st_running) return false;
    memset(&s_st, 0, sizeof s_st);
    s_st_running = true;
    if (xTaskCreatePinnedToCore(st_task, "ui_selftest", 6144, NULL, 2, NULL, 0) != pdPASS) {
        s_st_running = false;
        return false;
    }
    return true;
}

void ui_port_selftest_status(ui_selftest_status_t *out)
{
    *out = s_st;
    out->running = s_st_running;
}

/* ---- Wi-Fi ---- */
_Static_assert(sizeof(ui_wifi_ap_t) == sizeof(net_ap_t), "ui_wifi_ap_t mirrors net_ap_t");
bool ui_port_wifi_scan_start(void) { return net_scan_start(); }
int  ui_port_wifi_scan_results(ui_wifi_ap_t *out, int max)
{
    static net_ap_t tmp[20];
    if (max > 20) max = 20;
    int n = net_scan_results(tmp, max);
    for (int i = 0; i < n; i++) {
        memcpy(out[i].ssid, tmp[i].ssid, sizeof out[i].ssid);
        out[i].rssi = tmp[i].rssi;
        out[i].secure = tmp[i].secure;
    }
    return n;
}
void ui_port_wifi_send_test(void) { net_send_test(); }
