/* Fake backends for the PC UI simulator: app_state, the event bus with a pretend measurement
 * controller and periodic logger, storage (settings, a seeded result log) and ui_port.h.
 * Same headers as the device; only the implementations are fake. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim.h"
#include "app_state.h"
#include "app_events.h"
#include "storage.h"
#include "ui.h"
#include "ui_port.h"
#include "el_autolog.h"
#include "el_stats.h"
#include "el_time.h"
#include "app_config.h"

/* ---------------- clock ---------------- */
static int64_t s_epoch;                 /* Unix s at sim tick 0 */

int64_t sim_now(void) { return s_epoch + sim_tick_ms() / 1000; }

/* ---------------- app_state ---------------- */
static app_state_t s_state;
void app_state_init(void) {}
void app_state_get(app_state_t *out) { *out = s_state; }
void app_state_lock(void) {}
void app_state_unlock(void) {}
app_state_t *app_state_raw(void) { return &s_state; }

/* ---------------- storage ---------------- */
#define LOG_MAX 4000
static el_settings_t s_set;
static el_result_t   s_log[LOG_MAX];
static uint32_t      s_log_n, s_next_seq = 1;
static storage_export_result_t s_last_export;
static bool          s_sd;

void storage_settings_get(el_settings_t *out) { *out = s_set; }
el_err_t storage_settings_set(el_set_id_t id, const char *text, el_level_t who)
{
    el_err_t e = el_settings_set_str(&s_set, id, text, who);
    if (e == EL_OK) app_events_post(EVT_SETTINGS_CHANGED, SRC_INTERNAL, (int32_t)id);
    return e;
}

el_err_t storage_log_iterate(uint32_t start, uint32_t limit, el_datalog_dir_t dir, el_datalog_cb_t cb, void *ctx)
{
    for (uint32_t k = 0, i = start; k < limit && i < s_log_n; k++) {
        if (!cb(i, &s_log[i], ctx)) break;
        if (dir == EL_DATALOG_NEWEST_FIRST) { if (i == 0) break; i--; } else i++;
    }
    return EL_OK;
}
el_err_t storage_log_delete_all(el_level_t who)
{
    if (!el_access_allows(who, EL_ACT_DELETE_LOG)) return EL_ERR_DENIED;
    s_log_n = 0;
    s_state.log_count = 0;
    app_events_post(EVT_LOG_CHANGED, SRC_INTERNAL, 0);
    return EL_OK;
}
void storage_log_status(uint32_t *count, uint8_t *fill, bool *near, bool *full)
{
    if (count) *count = s_log_n;
    if (fill) *fill = (uint8_t)(s_log_n * 100u / 50000u);
    if (near) *near = false;
    if (full) *full = false;
}
void storage_event_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("[event] ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}
void storage_export_last(storage_export_result_t *out) { *out = s_last_export; }
bool storage_sd_present(void) { return s_sd; }

static void log_append(el_result_t *r)
{
    if (s_log_n == LOG_MAX) { memmove(s_log, s_log + 1, sizeof s_log[0] * (LOG_MAX - 1)); s_log_n--; }
    r->seq = s_next_seq++;
    s_log[s_log_n++] = *r;
    s_state.log_count = s_log_n;
    s_state.log_fill_pct = (uint8_t)(s_log_n * 100u / 50000u);
}

/* ---------------- fake results ---------------- */
static uint32_t s_rng = 12345;
static float frand(void) { s_rng = s_rng * 1664525u + 1013904223u; return (float)(s_rng >> 8) / 16777216.0f; }

static void make_result(el_result_t *r, float temp, float rh, uint8_t repeats, int64_t t, bool automatic)
{
    memset(r, 0, sizeof *r);
    r->timestamp_s = t;
    snprintf(r->tag, sizeof r->tag, "%s", s_set.location);
    if (!automatic) snprintf(r->operator_id, sizeof r->operator_id, "%s", s_set.operator_id);
    r->repeats = repeats;
    float vt[10], vh[10];
    for (int i = 0; i < repeats; i++) {
        vt[i] = temp + (frand() - 0.5f) * 0.06f;
        vh[i] = rh + (frand() - 0.5f) * 0.3f;
    }
    el_stats_t st, sh;
    el_stats_compute(vt, repeats, &st);
    el_stats_compute(vh, repeats, &sh);
    r->temp_c = st.mean;
    r->temp_sd_c = st.sd;
    r->rh_pct = sh.mean;
    r->rh_sd_pct = sh.sd;
    r->dew_point_c = el_dew_point_c(r->temp_c, r->rh_pct);
    r->limits = el_settings_limits(&s_set);
    r->status = el_classify(&r->limits, r->temp_c, r->rh_pct, &r->alarms);
    r->flags = EL_RES_FLAG_SIMULATED | (automatic ? EL_RES_FLAG_AUTO : 0);
}

/* A room over three days: daily temperature swing, humidity moving against it, one warm spell. */
static void seed(void)
{
    int64_t step = 1800;                                    /* every 30 min */
    for (int64_t t = s_epoch - 3 * 86400; t <= s_epoch - 600; t += step) {
        double day = (double)((t % 86400 + 86400) % 86400) / 86400.0;
        float temp = 21.5f + 2.2f * (float)sin(2.0 * 3.14159265 * (day - 0.35)) + (frand() - 0.5f) * 0.3f;
        if (t > s_epoch - 40 * 3600 && t < s_epoch - 36 * 3600) temp += 13.5f;   /* the warm spell (alarm) */
        float rh = 48.0f - (temp - 21.5f) * 2.5f + (frand() - 0.5f) * 1.5f;
        el_result_t r;
        make_result(&r, temp, rh, 3, t, true);
        log_append(&r);
    }
}

/* ---------------- event bus + pretend controller ---------------- */
#define EQ 64
static app_event_t s_q[EQ];
static int s_qh, s_qt;

bool app_events_post(app_event_id_t id, app_event_src_t src, int32_t arg)
{
    int nt = (s_qt + 1) % EQ;
    if (nt == s_qh) return false;
    s_q[s_qt] = (app_event_t){ id, src, arg };
    s_qt = nt;
    return true;
}
uint32_t app_events_dropped(void) { return 0; }

static struct { bool active, automatic; uint32_t t0, dur; } s_run;
static el_autolog_t s_auto;

static void start_run(bool automatic)
{
    if (s_run.active) { app_events_post(EVT_MEAS_REJECTED, SRC_INTERNAL, EL_MEAS_REJ_BUSY); return; }
    s_run.active = true;
    s_run.automatic = automatic;
    s_run.t0 = sim_tick_ms();
    s_run.dur = 400u * s_set.repeats;
    s_state.meas_source = automatic ? EL_MEAS_SRC_AUTO : EL_MEAS_SRC_UI;
    s_state.meas_state = EL_MEAS_MEASURING;
    s_state.meas_progress_pct = 0;
    s_state.meas_repeats = s_set.repeats;
    s_state.meas_repeat = 0;
    app_events_post(EVT_MEAS_STARTED, SRC_INTERNAL, s_state.meas_source);
}

static void finish_run(void)
{
    s_run.active = false;
    el_result_t r;
    float temp = 22.0f + (frand() - 0.5f) * 0.8f;
    make_result(&r, temp, 46.0f + (frand() - 0.5f) * 3.0f, s_set.repeats, sim_now(), s_run.automatic);
    log_append(&r);
    s_state.last_result = r;
    s_state.has_result = true;
    s_state.result_counter++;
    s_state.meas_state = EL_MEAS_IDLE;
    s_state.last_fault = EL_MEAS_FAULT_NONE;
    app_events_post(EVT_MEAS_DONE, SRC_INTERNAL, r.status);
    app_events_post(EVT_LOG_CHANGED, SRC_INTERNAL, 0);
}

static void controller(const app_event_t *e)
{
    switch (e->id) {
    case EVT_REQ_MEASURE: start_run(false); break;
    case EVT_REQ_CANCEL:
        if (s_run.active) { s_run.active = false; s_state.meas_state = EL_MEAS_IDLE; app_events_post(EVT_MEAS_CANCELLED, SRC_INTERNAL, 0); }
        break;
    case EVT_REQ_EXPORT:
        s_state.export_busy = true;
        s_state.export_progress_pct = 0;
        break;
    case EVT_REQ_DISCARD_RESULT: s_state.result_unsaved = false; break;
    case EVT_SETTINGS_CHANGED: el_autolog_set_interval(&s_auto, s_set.log_interval_s, sim_tick_ms()); break;
    default: break;
    }
}

void sim_backend_step(void)
{
    uint32_t now = sim_tick_ms();
    if (!s_run.active && s_state.meas_state == EL_MEAS_IDLE && el_autolog_due(&s_auto, now)) {
        start_run(true);
        el_autolog_taken(&s_auto, now);
    }
    s_state.next_auto_s = el_autolog_remaining_s(&s_auto, now);
    if (s_run.active) {
        uint32_t el = now - s_run.t0;
        uint8_t pct = el >= s_run.dur ? 100 : (uint8_t)(el * 100u / s_run.dur);
        s_state.meas_repeat = (uint8_t)(el / 400 + 1);
        if (s_state.meas_repeat > s_state.meas_repeats) s_state.meas_repeat = s_state.meas_repeats;
        if (pct != s_state.meas_progress_pct) {
            s_state.meas_progress_pct = pct;
            app_events_post(EVT_MEAS_PROGRESS, SRC_INTERNAL, pct);
        }
        if (el >= s_run.dur) finish_run();
    }
    if (s_state.export_busy) {
        static uint32_t last;
        if (now - last > 100) {
            last = now;
            if (s_state.export_progress_pct >= 100) {
                s_state.export_busy = false;
                s_last_export = (storage_export_result_t){ EL_OK, s_log_n, false, "ENVLOG_EL-0001_20261101-103000.csv" };
                app_events_post(EVT_EXPORT_DONE, SRC_INTERNAL, 0);
            } else {
                s_state.export_progress_pct += 5;
                app_events_post(EVT_EXPORT_PROGRESS, SRC_INTERNAL, s_state.export_progress_pct);
            }
        }
    }
    s_state.uptime_s = now / 1000 + 3600 * 5;
    s_state.sd_present = s_sd;
    while (s_qh != s_qt) {
        app_event_t e = s_q[s_qh];
        s_qh = (s_qh + 1) % EQ;
        controller(&e);
        ui_notify(e.id, e.arg);
    }
}

/* ---------------- scenario helpers for screenshots ---------------- */
void sim_set_sd(bool present) { s_sd = present; s_state.sd_present = present; }
void sim_set_result(float temp_c, float rh_pct, uint8_t repeats, bool automatic)
{
    el_result_t r;
    make_result(&r, temp_c, rh_pct, repeats, sim_now() - 20, automatic);
    r.seq = s_next_seq;
    s_state.last_result = r;
    s_state.has_result = true;
    s_state.meas_state = EL_MEAS_IDLE;
    s_state.last_fault = EL_MEAS_FAULT_NONE;
}
void sim_set_fault(el_meas_fault_t f)
{
    s_state.meas_state = EL_MEAS_FAULT;
    s_state.meas_source = EL_MEAS_SRC_UI;
    s_state.last_fault = f;
}
void sim_set_theme(bool dark)
{
    storage_settings_set(EL_SET_THEME, dark ? "DARK" : "LIGHT", EL_LEVEL_ENGINEER);
}
void sim_set_measuring(uint8_t pct, uint8_t rep)
{
    s_state.meas_source = EL_MEAS_SRC_UI;
    s_state.meas_state = EL_MEAS_MEASURING;
    s_state.meas_progress_pct = pct;
    s_state.meas_repeats = s_set.repeats;
    s_state.meas_repeat = rep;
}
void sim_set_export(bool busy, uint8_t pct) { s_state.export_busy = busy; s_state.export_progress_pct = pct; }
void sim_clear_queue(void) { s_qh = s_qt; }

void sim_backend_init(void)
{
    el_datetime_t dt = { 2026, 11, 1, 10, 30, 0 };
    el_datetime_to_time(&dt, &s_epoch);
    el_settings_defaults(&s_set);
    el_settings_set_str(&s_set, EL_SET_DEVICE_SERIAL, "EL-0001", EL_LEVEL_ENGINEER);
    el_settings_set_str(&s_set, EL_SET_OPERATOR_ID, "JD", EL_LEVEL_ENGINEER);
    el_settings_set_str(&s_set, EL_SET_UTC_OFFSET, "-240", EL_LEVEL_ENGINEER);
    el_settings_set_str(&s_set, EL_SET_LOCATION, "Archive room B", EL_LEVEL_ENGINEER);
    el_settings_set_str(&s_set, EL_SET_T_LOW, "16", EL_LEVEL_ENGINEER);
    el_settings_set_str(&s_set, EL_SET_T_HIGH, "28", EL_LEVEL_ENGINEER);
    el_settings_set_str(&s_set, EL_SET_RH_LOW, "30", EL_LEVEL_ENGINEER);
    el_settings_set_str(&s_set, EL_SET_RH_HIGH, "60", EL_LEVEL_ENGINEER);
    el_settings_set_str(&s_set, EL_SET_LOG_INTERVAL, "600", EL_LEVEL_ENGINEER);
    seed();
    el_autolog_init(&s_auto, s_set.log_interval_s, 0);
    s_auto.next_ms = 252000;                                   /* next reading in ~4 min */

    memset(&s_state, 0, sizeof s_state);
    s_state.log_count = s_log_n;
    s_state.log_fill_pct = (uint8_t)(s_log_n * 100u / 50000u);
    s_state.sensor_simulated = true;
    s_state.rtc_valid = true;
    s_state.usb_pc_connected = true;
    s_sd = true;
    s_state.sd_present = true;
    if (s_log_n) { s_state.last_result = s_log[s_log_n - 1]; s_state.has_result = true; }
}

/* ---------------- ui_port.h ---------------- */
bool ui_port_lock(uint32_t t) { (void)t; return true; }
void ui_port_unlock(void) {}
uint32_t ui_port_ms(void) { return sim_tick_ms(); }
int64_t ui_port_now(void) { return sim_now(); }
void ui_port_backlight(uint8_t pct) { sim_backlight(pct); }
bool ui_port_backlight_dimmable(void) { return false; }
el_err_t ui_port_set_time(int64_t utc)
{
    s_epoch = utc - sim_tick_ms() / 1000;
    s_state.rtc_valid = true;
    app_events_post(EVT_TIME_SET, SRC_UI, 0);
    return EL_OK;
}
bool ui_port_engineer_login_enabled(void) { return true; }
const char *ui_port_fw_version(void) { return "0.1.0-sim"; }
const char *ui_port_board_rev(void) { return "Waveshare 4.3B"; }
bool ui_port_fw_file(char *ver, size_t cap) { snprintf(ver, cap, "0.2.0"); return true; }
void ui_port_sensor_info(char *buf, size_t cap) { snprintf(buf, cap, "Simulated"); }
void ui_port_diag(ui_diag_t *d)
{
    memset(d, 0, sizeof *d);
    d->heap_free = 182 * 1024;
    d->heap_min = 150 * 1024;
    d->psram_free = 6100 * 1024;
    d->data_free = 9200000;
    d->data_total = 12000000;
    if (s_sd) { d->sd_free = 15300000000ull; d->sd_total = 15900000000ull; }
    snprintf(d->i2c_text, sizeof d->i2c_text, "IO RTC TP SHT!");
}
static ui_selftest_status_t s_st = { false, true, 10, 1, 0, 2, "" };
bool ui_port_selftest_start(void) { s_st.done = true; return true; }
void ui_port_selftest_status(ui_selftest_status_t *out) { *out = s_st; }

/* ---- Wi-Fi (fake) ---- */
static const ui_wifi_ap_t SIM_APS[] = {
    { "Lab-Network", -48, true }, { "Plant Floor 2.4", -63, true },
    { "Guest", -71, false }, { "Maintenance-Office", -82, true },
};
static int s_scan_polls = -1;
bool ui_port_wifi_scan_start(void) { s_scan_polls = 2; return true; }
int ui_port_wifi_scan_results(ui_wifi_ap_t *out, int max)
{
    if (s_scan_polls > 0) { s_scan_polls--; return -1; }
    int n = (int)(sizeof SIM_APS / sizeof SIM_APS[0]);
    if (n > max) n = max;
    memcpy(out, SIM_APS, (size_t)n * sizeof *out);
    return n;
}
void ui_port_wifi_send_test(void) { s_state.uplink_last_ok_s = sim_now(); app_events_post(EVT_NET_CHANGED, SRC_INTERNAL, 0); }

void sim_set_wifi(int state, uint32_t pending, const char *error)
{
    bool on = state != NET_OFF;
    el_settings_set_str(&s_set, EL_SET_WIFI_ENABLED, on ? "1" : "0", EL_LEVEL_ENGINEER);
    if (state != NET_NOT_SET_UP && on) {
        el_settings_set_str(&s_set, EL_SET_WIFI_SSID, "Lab-Network", EL_LEVEL_ENGINEER);
        el_settings_set_str(&s_set, EL_SET_WIFI_PASSWORD, "correct horse", EL_LEVEL_ENGINEER);
        el_settings_set_str(&s_set, EL_SET_UPLOAD_URL, "https://data.example.com/readings", EL_LEVEL_ENGINEER);
        el_settings_set_str(&s_set, EL_SET_UPLOAD_KEY, "demo-key-123", EL_LEVEL_ENGINEER);
    }
    s_state.net_state = (net_state_t)state;
    s_state.net_rssi = state == NET_CONNECTED ? -52 : 0;
    snprintf(s_state.net_ip, sizeof s_state.net_ip, "%s", state == NET_CONNECTED ? "192.168.1.47" : "");
    snprintf(s_state.net_ssid, sizeof s_state.net_ssid, "%s", on ? s_set.wifi_ssid : "");
    snprintf(s_state.net_error, sizeof s_state.net_error, "%s", error ? error : "");
    s_state.uplink_pending = pending;
    s_state.uplink_last_ok_s = state == NET_CONNECTED ? sim_now() - 300 : 0;
    app_events_post(EVT_SETTINGS_CHANGED, SRC_INTERNAL, -1);
    app_events_post(EVT_NET_CHANGED, SRC_INTERNAL, 0);
}
