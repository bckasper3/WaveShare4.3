#pragma once
/* Single shared snapshot of device state (docs/FIRMWARE_ARCH.md). Each field has ONE writer
 * (named in its comment). Writers: app_state_lock(), update, app_state_unlock(), then post an
 * event. Readers (UI, serial, Modbus): app_state_get() copies the whole struct under the lock.
 * Never hold the lock across I/O or slow work. Built from envlogger_core types so the PC UI
 * simulator can fill it without any ESP-IDF code. */
#include <stdbool.h>
#include <stdint.h>
#include "el_result.h"
#include "el_meas.h"
#include "el_access.h"

/* Wi-Fi (send-only upload, docs/WIFI.md) */
typedef enum { NET_OFF = 0, NET_NOT_SET_UP, NET_CONNECTING, NET_CONNECTED, NET_FAILED } net_state_t;

typedef struct {
    /* measurement - writer: app controller (app_controller.c) */
    el_meas_state_t  meas_state;
    el_meas_src_t    meas_source;                 /* who started the current / last run */
    uint8_t          meas_progress_pct;
    uint8_t          meas_repeat, meas_repeats;   /* "reading 2 of 3" */
    el_meas_fault_t  last_fault;                  /* EL_MEAS_FAULT_NONE if the last run was OK */
    el_result_t      last_result;                 /* complete: timestamp, tag, seq filled */
    bool             has_result;
    bool             result_unsaved;              /* last_result not in the log yet (retrying) */
    uint32_t         result_counter;              /* results produced since boot (Modbus reg 3) */
    uint32_t         next_auto_s;                 /* s until the next automatic reading; UINT32_MAX = off */

    /* log - writer: housekeeping loop in main.c (from the storage service) */
    uint32_t         log_count;
    uint8_t          log_fill_pct;
    bool             log_near_full, log_full;
    bool             export_busy;
    uint8_t          export_progress_pct;        /* writer: export task */
    bool             sd_present;

    /* USB - writer: USB service */
    bool             usb_pc_connected;   /* CDC port opened (DTR) */

    /* Wi-Fi upload - writer: net task (main/net) */
    net_state_t      net_state;
    int8_t           net_rssi;           /* dBm, 0 = unknown */
    char             net_ip[16];         /* "" when not connected */
    char             net_ssid[33];       /* network in use / being tried */
    char             net_error[48];      /* last connection or upload problem, "" = none */
    uint32_t         uplink_pending;     /* results not yet accepted by the server */
    int64_t          uplink_last_ok_s;   /* Unix s of the last accepted upload, 0 = never */
    bool             uplink_busy;        /* a POST is in flight */
    bool             uplink_rejected;    /* server refused (wrong address / key): slow retry */

    /* customer digital I/O - writer: dio task */
    bool             di0, di1;           /* true = input active (polarity already corrected) */
    bool             do1;                /* true = output ON */

    /* device status - writer: app controller at boot, then the owning service */
    bool             sensor_present;
    bool             sensor_simulated;
    bool             rtc_valid;
    uint16_t         boot_selftest_fail; /* number of FAILs in the boot self-test */
    el_level_t       ui_level;           /* touchscreen session level - writer: UI */
    uint32_t         uptime_s;           /* writer: housekeeping tick in main.c */
} app_state_t;

void         app_state_init(void);
void         app_state_get(app_state_t *out);        /* copy under lock */
void         app_state_lock(void);
void         app_state_unlock(void);
app_state_t *app_state_raw(void);                     /* only between lock/unlock */
