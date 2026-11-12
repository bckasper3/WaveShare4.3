#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "dio.h"
#include "board.h"
#include "app_state.h"
#include "app_events.h"
#include "storage.h"
#include "el_app_dio.h"

static const char *TAG = "dio";
static SemaphoreHandle_t s_mtx;
static el_app_do1_t s_do1;
static bool s_busy, s_trigger, s_out_known, s_out;

/* Drive DO1 if the wanted state changed. Caller holds s_mtx. */
static void apply_locked(void)
{
    bool want = el_app_do1_output(&s_do1, s_busy);
    if (s_out_known && want == s_out) return;
    if (board_set_do1(want) == ESP_OK) {
        s_out = want;
        s_out_known = true;
        app_state_lock();
        app_state_raw()->do1 = want;
        app_state_unlock();
    }
}

static void load_settings(void)
{
    el_settings_t s;
    storage_settings_get(&s);
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    el_app_do1_set_mode(&s_do1, s.do1_mode);
    s_trigger = s.di0_trigger;
    apply_locked();
    xSemaphoreGive(s_mtx);
}

void dio_on_result(el_result_status_t st)
{
    if (!s_mtx) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    el_app_do1_result(&s_do1, st);
    apply_locked();
    xSemaphoreGive(s_mtx);
}

void dio_set_meas_busy(bool busy)
{
    if (!s_mtx) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (busy != s_busy) { s_busy = busy; apply_locked(); }
    xSemaphoreGive(s_mtx);
}

void dio_clear_alarm(void)
{
    if (!s_mtx) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    el_app_do1_clear(&s_do1);
    apply_locked();
    xSemaphoreGive(s_mtx);
}

static void on_event(const app_event_t *e, void *ctx)
{
    (void)ctx;
    if (e->id == EVT_SETTINGS_CHANGED) load_settings();
}

static void dio_task(void *arg)
{
    (void)arg;
    el_app_di_t di;
    el_app_di_init(&di);
    for (;;) {
        bool d0 = false, d1 = false;
        if (board_read_di(&d0, &d1) == ESP_OK) {
            el_app_di_ev_t ev;
            el_app_di_update(&di, d0, d1, &ev);
            if (ev.changed) {
                app_state_lock();
                app_state_raw()->di0 = di.stable[0];
                app_state_raw()->di1 = di.stable[1];
                app_state_unlock();
                app_events_post(EVT_DI_CHANGED, SRC_DI, ev.bits);
            }
            if (ev.rise[0] && s_trigger) app_events_post(EVT_REQ_MEASURE, SRC_DI, 0);
        }
        board_io_t io;
        board_get_io(&io);
        vTaskDelay(pdMS_TO_TICKS(el_app_dio_period_ms(io.backlight, s_trigger)));
    }
}

esp_err_t dio_start(void)
{
    if (s_mtx) return ESP_OK;
    s_mtx = xSemaphoreCreateMutex();
    el_settings_t s;
    storage_settings_get(&s);
    el_app_do1_init(&s_do1, s.do1_mode);
    s_trigger = s.di0_trigger;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    apply_locked();                       /* DO1 off at start (no result yet) */
    xSemaphoreGive(s_mtx);
    app_events_add_listener(on_event, NULL);
    if (xTaskCreatePinnedToCore(dio_task, "dio", 3072, NULL, 3, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
