#pragma once
/* What the UI needs from the platform, and nothing more. The UI never includes a driver header:
 * it reads app_state, posts app_events, reads/writes data through storage.h, and calls these.
 *   device:    ui_port_esp.c   (ESP-IDF; *_esp.c files are left out of the PC simulator)
 *   simulator: sim/sim_port.c  (fake data, virtual clock)
 * All functions are safe to call from the LVGL task and return quickly. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_err.h"
#include "el_access.h"

/* LVGL lock for callers outside the LVGL task (display_lock on the device). */
bool     ui_port_lock(uint32_t timeout_ms);
void     ui_port_unlock(void);

uint32_t ui_port_ms(void);                 /* monotonic ms */
int64_t  ui_port_now(void);                /* Unix s (UTC) */

void     ui_port_backlight(uint8_t pct);   /* 0 = off. The 4.3B backlight is on/off only */
bool     ui_port_backlight_dimmable(void); /* false: hide the brightness and dim settings */

/* RTC + system clock. EL_ERR_RANGE outside 2000..2099. Posts EVT_TIME_SET. */
el_err_t ui_port_set_time(int64_t utc);

/* Engineer login is offered (code: setting engineer_code). Kept as a hook to hide it per build. */
bool     ui_port_engineer_login_enabled(void);

/* ---- About / firmware update ---- */
const char *ui_port_fw_version(void);
const char *ui_port_board_rev(void);
/* A FIRMWARE.BIN is on the SD card; its version in ver (may be ""). */
bool     ui_port_fw_file(char *ver, size_t cap);
void     ui_port_sensor_info(char *buf, size_t cap);

/* ---- Wi-Fi (send-only upload; main/net on the device) ---- */
typedef struct {
    char   ssid[33];
    int8_t rssi;                 /* dBm */
    bool   secure;
} ui_wifi_ap_t;
/* Start a network scan (the radio runs briefly even if Wi-Fi is off). false = already scanning. */
bool     ui_port_wifi_scan_start(void);
/* -1 while scanning, else networks copied (strongest first). */
int      ui_port_wifi_scan_results(ui_wifi_ap_t *out, int max);
/* Send a test message to the upload server as soon as Wi-Fi is connected. */
void     ui_port_wifi_send_test(void);

/* ---- Diagnostics ---- */
typedef struct {
    uint32_t heap_free, heap_min, psram_free;
    uint64_t data_free, data_total;          /* internal FAT */
    uint64_t sd_free, sd_total;              /* 0/0 when no card */
    bool     sd_mirror_behind;
    uint32_t sd_mirror_errors, events_dropped;
    char     i2c_text[64];                   /* known devices, e.g. "IO RTC TP SHT" */
    uint32_t events_lost;                    /* app_events queue overflows */
} ui_diag_t;
void     ui_port_diag(ui_diag_t *d);

/* Background self-test (BOOT group). start() returns false if one is already running. */
typedef struct {
    bool     running, done;
    uint16_t pass, warn, fail, skip;
    char     first_fail[24];
} ui_selftest_status_t;
bool     ui_port_selftest_start(void);
void     ui_port_selftest_status(ui_selftest_status_t *out);
