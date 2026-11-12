/* App controller task (FIRMWARE_ARCH.md section 2): drains the event queue, owns el_meas and the
 * periodic logger (el_autolog), logs every result (never dropped silently: el_app_retry), drives
 * the DO1 busy/alarm state and fans every event out to the listeners (UI, console stream, ...).
 * All measurement logic is el_meas / el_autolog / el_app_result (host-tested); this file wires
 * it up. */
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app_state.h"
#include "app_events.h"
#include "app_events_priv.h"
#include "app_config.h"
#include "storage.h"
#include "sensor.h"
#include "dio.h"
#include "fw_update.h"
#include "el_app_result.h"
#include "el_autolog.h"
#include "el_meas.h"
#include "el_util.h"

static const char *TAG = "ctrl";
#define STEP_MS 20

static el_meas_t s_meas;
static el_app_retry_t s_retry;
static el_autolog_t s_auto;
static el_settings_t s_set;
static TaskHandle_t s_task;

/* Identity of the accepted run, snapshotted at acceptance. */
static struct {
    int64_t  ts;
    char     tag[EL_TAG_LEN];
    char     op[EL_OPERATOR_LEN];
} s_run;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static el_meas_src_t to_src(app_event_src_t s)
{
    switch (s) {
    case SRC_UI: return EL_MEAS_SRC_UI;
    case SRC_SERIAL: return EL_MEAS_SRC_SERIAL;
    case SRC_MODBUS: return EL_MEAS_SRC_MODBUS;
    case SRC_DI: return EL_MEAS_SRC_DI;
    default: return EL_MEAS_SRC_AUTO;
    }
}

static void reload_config(void)
{
    storage_settings_get(&s_set);
    el_autolog_set_interval(&s_auto, s_set.log_interval_s, now_ms());
}

/* SIM ON|OFF takes effect at the next request, when no run is active (el_meas_set_sensor). */
static bool s_drv_sim = true;
static void apply_sensor_choice(void)
{
    bool sim = sensor_simulated();
    if (sim == s_drv_sim || el_meas_busy(&s_meas)) return;
    if (el_meas_set_sensor(&s_meas, sensor_driver()) != EL_OK) return;
    s_drv_sim = sim;
    app_state_lock();
    app_state_raw()->sensor_simulated = sim;
    app_state_unlock();
    storage_event_log("sensor: %s", sim ? "simulated" : "SHT");
}

/* Returns the verdict (the rejection is also posted as an event). */
static el_meas_reject_t start_measurement(app_event_src_t src)
{
    apply_sensor_choice();
    bool near, full;
    uint32_t cnt;
    uint8_t fill;
    storage_log_status(&cnt, &fill, &near, &full);
    el_app_conditions_t c = { .export_busy = storage_export_busy(), .log_full = full };
    el_meas_env_t env;
    el_app_meas_env(&env, &s_set, &c);
    el_meas_reject_t v = el_meas_request(&s_meas, EL_MEAS_REQ_MEASURE, to_src(src), &env, now_ms());
    if (v != EL_MEAS_ACCEPTED) {
        ESP_LOGI(TAG, "measure (%s) rejected: %s", el_meas_src_name(to_src(src)), el_meas_reject_name(v));
        app_events_post(EVT_MEAS_REJECTED, src == SRC_AUTO ? SRC_AUTO : SRC_INTERNAL, (int32_t)v);
        return v;
    }
    /* Identity is fixed now; settings changed mid-run do not change it. */
    s_run.ts = (int64_t)time(NULL);
    el_strlcpy(s_run.tag, s_set.location, sizeof s_run.tag);
    el_strlcpy(s_run.op, src == SRC_AUTO ? "" : s_set.operator_id, sizeof s_run.op);
    return v;
}

static void handle_meas_request(const app_event_t *e)
{
    if (e->id == EVT_REQ_CANCEL) {
        el_meas_reject_t v = el_meas_request(&s_meas, EL_MEAS_REQ_CANCEL, to_src(e->src), NULL, now_ms());
        if (v != EL_MEAS_ACCEPTED) app_events_post(EVT_MEAS_REJECTED, SRC_INTERNAL, (int32_t)v);
        return;
    }
    start_measurement(e->src);
}

static void publish_result(const el_result_t *r, bool saved)
{
    app_state_lock();
    app_state_t *st = app_state_raw();
    st->last_result = *r;
    st->has_result = true;
    st->result_unsaved = !saved;
    app_state_unlock();
}

/* Try to log the pending result. true = logged and acked. */
static bool try_log(void)
{
    const el_result_t *p = el_meas_result(&s_meas);
    if (!p) { el_app_retry_clear(&s_retry); return true; }
    el_result_t r = *p;
    if (storage_log_append(&r) != EL_OK) return false;
    publish_result(&r, true);
    el_meas_ack(&s_meas);
    el_app_retry_clear(&s_retry);
    return true;
}

static void on_done(el_meas_out_t *o)
{
    el_result_t *r = &o->result;
    el_app_result_meta_t m = { .timestamp_s = s_run.ts, .tag = s_run.tag, .operator_id = s_run.op };
    el_app_result_fill(r, &m);
    /* el_meas_result() must carry the same fields for a retry. */
    el_result_t *pending = (el_result_t *)el_meas_result(&s_meas);
    if (pending) *pending = *r;

    app_state_lock();
    app_state_raw()->result_counter++;
    app_state_unlock();
    dio_on_result(r->status);

    el_result_t logged = *r;
    if (storage_log_append(&logged) == EL_OK) {
        publish_result(&logged, true);
        el_meas_ack(&s_meas);
        app_events_post(EVT_MEAS_DONE, SRC_INTERNAL, (int32_t)r->status);
    } else {
        ESP_LOGE(TAG, "log append failed: result kept, retrying");
        publish_result(r, false);
        el_app_retry_start(&s_retry, now_ms());
        app_events_post(EVT_MEAS_DONE, SRC_INTERNAL, (int32_t)r->status);
        app_events_post(EVT_LOG_APPEND_FAILED, SRC_INTERNAL, 0);
        storage_event_log("result not saved (log append failed), retrying");
    }
}

/* Periodic logging: a due slot starts a run when the engine is idle; a refused slot is skipped. */
static void autolog_tick(uint32_t now)
{
    if (el_meas_state(&s_meas) != EL_MEAS_IDLE || !el_autolog_due(&s_auto, now)) return;
    el_meas_reject_t v = start_measurement(SRC_AUTO);
    el_autolog_taken(&s_auto, now);
    if (v != EL_MEAS_ACCEPTED) storage_event_log("automatic reading skipped: %s", el_meas_reject_name(v));
}

static void step(void)
{
    uint32_t now = now_ms();
    autolog_tick(now);

    el_meas_out_t o;
    el_meas_step(&s_meas, now, &o);

    app_state_lock();
    app_state_t *st = app_state_raw();
    st->meas_state = o.state;
    st->meas_source = o.source;
    st->meas_progress_pct = o.progress_pct;
    st->meas_repeat = o.repeat;
    st->meas_repeats = o.repeats;
    st->next_auto_s = el_autolog_remaining_s(&s_auto, now);
    if (o.events & EL_MEAS_EV_FAULT) st->last_fault = o.fault;
    if (o.events & EL_MEAS_EV_DONE) st->last_fault = EL_MEAS_FAULT_NONE;
    app_state_unlock();
    dio_set_meas_busy(o.busy);

    if (o.events & EL_MEAS_EV_STARTED)   app_events_post(EVT_MEAS_STARTED, SRC_INTERNAL, (int32_t)o.source);
    if (o.events & EL_MEAS_EV_PROGRESS)  app_events_post(EVT_MEAS_PROGRESS, SRC_INTERNAL, o.progress_pct);
    if (o.events & EL_MEAS_EV_CANCELLED) app_events_post(EVT_MEAS_CANCELLED, SRC_INTERNAL, 0);
    if (o.events & EL_MEAS_EV_FAULT) {
        ESP_LOGW(TAG, "fault %s", el_meas_fault_name(o.fault));
        storage_event_log("measurement fault %s", el_meas_fault_name(o.fault));
        app_events_post(EVT_MEAS_FAULT, SRC_INTERNAL, (int32_t)o.fault);
        el_meas_ack(&s_meas);     /* fault shown via the event; next request starts clean */
    }
    if (o.events & EL_MEAS_EV_DONE) on_done(&o);

    if (el_app_retry_due(&s_retry, now)) {
        if (try_log()) {
            ESP_LOGI(TAG, "pending result logged on retry");
            app_events_post(EVT_LOG_CHANGED, SRC_INTERNAL, 0);
        } else {
            el_app_retry_failed(&s_retry, now);
            if (el_app_retry_exhausted(&s_retry))
                storage_event_log("result still not saved after %u retries", EL_APP_LOG_RETRIES);
        }
    }
}

static void handle(const app_event_t *e)
{
    switch (e->id) {
    case EVT_REQ_MEASURE:
    case EVT_REQ_CANCEL:
        handle_meas_request(e);
        break;
    case EVT_REQ_DISCARD_RESULT:
        if (el_meas_state(&s_meas) == EL_MEAS_DONE) {
            if (!try_log()) {             /* one last attempt, then drop it on request */
                el_meas_ack(&s_meas);
                el_app_retry_clear(&s_retry);
                storage_event_log("unsaved result discarded by operator");
            }
            app_state_lock();
            app_state_raw()->result_unsaved = false;
            app_state_unlock();
        }
        break;
    case EVT_REQ_CLEAR_ALARM:
        dio_clear_alarm();
        break;
    case EVT_REQ_EXPORT:
        if (storage_export_start((storage_export_scope_t)(e->arg & 0xFF)) != ESP_OK)
            app_events_post(EVT_EXPORT_DONE, SRC_INTERNAL, EL_ERR_IO);
        break;
    case EVT_REQ_FW_UPDATE:
        fw_update_from_sd_async();
        break;
    case EVT_SETTINGS_CHANGED:
    case EVT_TIME_SET:
        reload_config();
        break;
    default:
        break;
    }
}

static void controller_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        app_event_t e;
        if (app_events_wait(&e, STEP_MS)) {
            handle(&e);
            app_events_dispatch(&e);
        }
        if (xTaskGetTickCount() - last >= pdMS_TO_TICKS(STEP_MS)) {
            last = xTaskGetTickCount();
            step();
        }
    }
}

void app_controller_start(void)
{
    if (s_task) return;
    sensor_init();
    el_meas_init(&s_meas, sensor_driver());
    s_drv_sim = sensor_simulated();
    el_app_retry_clear(&s_retry);
    storage_settings_get(&s_set);
    el_autolog_init(&s_auto, s_set.log_interval_s, now_ms());
    app_state_lock();
    app_state_raw()->sensor_present = sensor_present();
    app_state_raw()->sensor_simulated = sensor_simulated();
    app_state_raw()->next_auto_s = el_autolog_remaining_s(&s_auto, now_ms());
    app_state_unlock();
    xTaskCreatePinnedToCore(controller_task, "app_ctrl", 6144, NULL, 6, &s_task, 1);
}
