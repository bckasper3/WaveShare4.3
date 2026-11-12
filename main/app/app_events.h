#pragma once
/* Event bus (docs/FIRMWARE_ARCH.md). Anyone may post; the app controller task owns the queue,
 * acts on requests, and fans every event out to the registered listeners (UI, serial stream,
 * DO1 alarm, Modbus). Listeners run in the controller task: keep them short, never block. */
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EVT_NONE = 0,
    /* requests (from UI, serial, Modbus, DI0) - handled by the controller */
    EVT_REQ_MEASURE,
    EVT_REQ_CANCEL,
    EVT_REQ_EXPORT,           /* arg = storage_export_scope_t; the target is the SD card */
    EVT_REQ_FW_UPDATE,        /* update from the firmware file on the SD card */
    EVT_REQ_CLEAR_ALARM,      /* DO1 off until the next alarm */
    EVT_REQ_DISCARD_RESULT,   /* drop a result that could not be logged (APP.md section 2) */
    /* measurement notifications - posted by the controller */
    EVT_MEAS_STARTED,         /* arg = el_meas_src_t (AUTO = periodic logging) */
    EVT_MEAS_PROGRESS,        /* arg = 0..100 */
    EVT_MEAS_DONE,            /* arg = el_result_status_t; result in app_state.last_result */
    EVT_MEAS_FAULT,           /* arg = el_meas_fault_t */
    EVT_MEAS_CANCELLED,
    EVT_MEAS_REJECTED,        /* arg = el_meas_reject_t */
    EVT_LOG_APPEND_FAILED,    /* result kept in RAM, retrying (app_state.result_unsaved) */
    /* USB / removable media */
    EVT_USB_PC_CONNECTED,
    EVT_USB_PC_DISCONNECTED,
    EVT_SD_MOUNTED,
    EVT_SD_REMOVED,
    EVT_EXPORT_PROGRESS,      /* arg = 0..100 */
    EVT_EXPORT_DONE,          /* arg = 0 ok, else el_err_t */
    EVT_FW_UPDATE_PROGRESS,   /* arg = 0..100 */
    EVT_FW_UPDATE_DONE,       /* arg = 0 ok (reboot follows), else el_err_t */
    /* data / configuration */
    EVT_SETTINGS_CHANGED,     /* arg = el_set_id_t, or -1 for "many" */
    EVT_LOG_CHANGED,          /* appended or cleared */
    EVT_TIME_SET,
    EVT_DI_CHANGED,           /* arg = bit0 DI0, bit1 DI1 (active = 1) */
    EVT_NET_CHANGED,          /* Wi-Fi / upload state in app_state changed */
    EVT_COUNT
} app_event_id_t;

typedef enum { SRC_UI = 0, SRC_SERIAL, SRC_MODBUS, SRC_DI, SRC_INTERNAL, SRC_AUTO } app_event_src_t;

typedef struct {
    app_event_id_t  id;
    app_event_src_t src;
    int32_t         arg;
} app_event_t;

typedef void (*app_event_listener_t)(const app_event_t *e, void *ctx);

void app_events_init(void);
/* Non-blocking; false if the queue is full (the event is dropped and counted). */
bool app_events_post(app_event_id_t id, app_event_src_t src, int32_t arg);
/* Up to 8 listeners; register before app_controller_start(). */
bool app_events_add_listener(app_event_listener_t fn, void *ctx);
uint32_t app_events_dropped(void);
const char *app_event_name(app_event_id_t id);

/* Controller task: owns el_meas and the periodic logger, logs results, fans events out. */
void app_controller_start(void);
