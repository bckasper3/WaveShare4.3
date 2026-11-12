/* Event bus: one queue, drained by the controller task (app_controller.c), which acts on
 * requests and then hands every event to the registered listeners. */
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "app_events.h"
#include "app_events_priv.h"

#define MAX_LISTENERS 8

static QueueHandle_t s_q;
static struct { app_event_listener_t fn; void *ctx; } s_listeners[MAX_LISTENERS];
static size_t s_n_listeners;
static volatile uint32_t s_dropped;

void app_events_init(void)
{
    if (!s_q) s_q = xQueueCreate(32, sizeof(app_event_t));
}

bool app_events_post(app_event_id_t id, app_event_src_t src, int32_t arg)
{
    app_event_t e = { .id = id, .src = src, .arg = arg };
    if (s_q && xQueueSend(s_q, &e, 0) == pdTRUE) return true;
    s_dropped++;
    return false;
}

bool app_events_add_listener(app_event_listener_t fn, void *ctx)
{
    if (!fn || s_n_listeners >= MAX_LISTENERS) return false;
    s_listeners[s_n_listeners].fn = fn;
    s_listeners[s_n_listeners].ctx = ctx;
    s_n_listeners++;
    return true;
}

uint32_t app_events_dropped(void) { return s_dropped; }

bool app_events_wait(app_event_t *out, uint32_t timeout_ms)
{
    return s_q && xQueueReceive(s_q, out, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void app_events_dispatch(const app_event_t *e)
{
    for (size_t i = 0; i < s_n_listeners; i++) s_listeners[i].fn(e, s_listeners[i].ctx);
}

const char *app_event_name(app_event_id_t id)
{
    static const char *const N[EVT_COUNT] = {
        [EVT_NONE] = "NONE",
        [EVT_REQ_MEASURE] = "REQ_MEASURE", [EVT_REQ_CANCEL] = "REQ_CANCEL",
        [EVT_REQ_EXPORT] = "REQ_EXPORT", [EVT_REQ_FW_UPDATE] = "REQ_FW_UPDATE",
        [EVT_REQ_CLEAR_ALARM] = "REQ_CLEAR_ALARM", [EVT_REQ_DISCARD_RESULT] = "REQ_DISCARD_RESULT",
        [EVT_MEAS_STARTED] = "MEAS_STARTED", [EVT_MEAS_PROGRESS] = "MEAS_PROGRESS",
        [EVT_MEAS_DONE] = "MEAS_DONE", [EVT_MEAS_FAULT] = "MEAS_FAULT",
        [EVT_MEAS_CANCELLED] = "MEAS_CANCELLED", [EVT_MEAS_REJECTED] = "MEAS_REJECTED",
        [EVT_LOG_APPEND_FAILED] = "LOG_APPEND_FAILED",
        [EVT_USB_PC_CONNECTED] = "USB_PC_CONNECTED", [EVT_USB_PC_DISCONNECTED] = "USB_PC_DISCONNECTED",
        [EVT_SD_MOUNTED] = "SD_MOUNTED", [EVT_SD_REMOVED] = "SD_REMOVED",
        [EVT_EXPORT_PROGRESS] = "EXPORT_PROGRESS", [EVT_EXPORT_DONE] = "EXPORT_DONE",
        [EVT_FW_UPDATE_PROGRESS] = "FW_UPDATE_PROGRESS", [EVT_FW_UPDATE_DONE] = "FW_UPDATE_DONE",
        [EVT_SETTINGS_CHANGED] = "SETTINGS_CHANGED",
        [EVT_LOG_CHANGED] = "LOG_CHANGED", [EVT_TIME_SET] = "TIME_SET", [EVT_DI_CHANGED] = "DI_CHANGED",
        [EVT_NET_CHANGED] = "NET_CHANGED",
    };
    return (id < EVT_COUNT && N[id]) ? N[id] : "?";
}
