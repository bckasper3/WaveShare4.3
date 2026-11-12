#pragma once
/* Storage service: the ONLY module that touches NVS, the internal FAT partition and the SD
 * card. Thread-safe (one internal mutex). Wraps envlogger_core codecs: el_settings, el_datalog.
 *
 * Media (docs/FIRMWARE_ARCH.md section 4):
 *   internal flash  primary result log (el_datalog on FAT, WL 512 B SAFE), always present
 *   NVS             settings (incl. calibration offsets), counters
 *                   (each blob in two alternating slots: a torn write falls back to the other)
 *   SD card         OPTIONAL, removable: CSV mirror of every result, event/diagnostic log,
 *                   export target, firmware-update source. Device works fully without it.
 * Every public call returns quickly except log iteration and export/update (those are
 * documented as slow or run in their own task). */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "el_err.h"
#include "el_access.h"
#include "el_result.h"
#include "el_settings.h"
#include "el_datalog.h"
#include "el_selftest.h"

#define STORAGE_MOUNT      "/data"     /* internal FAT */
#define STORAGE_SD_MOUNT   "/sd"

typedef struct {
    el_datalog_report_t log_recovery;    /* what datalog open repaired */
    bool  nvs_ok, fat_ok, log_ok;
    bool  settings_defaulted;
    bool  sd_present;
} storage_boot_report_t;

/* Mount everything, open the log (crash recovery), load all blobs. Never fails hard: a broken
 * medium is reported and the device runs on defaults. */
esp_err_t storage_init(storage_boot_report_t *report);

/* ---- settings ---- */
void     storage_settings_get(el_settings_t *out);
/* Validate (el_settings_set_str), persist, post EVT_SETTINGS_CHANGED. EL_ERR_DENIED / _ARG / _RANGE / _IO. */
el_err_t storage_settings_set(el_set_id_t id, const char *text, el_level_t who);
el_err_t storage_settings_factory_reset(el_level_t who);       /* ENGINEER; keeps the serial */

/* ---- result log ---- */
/* Durable append to internal flash (r->seq assigned), then best-effort CSV line to the SD
 * mirror. EL_OK means the internal log has it. Posts EVT_LOG_CHANGED. */
el_err_t storage_log_append(el_result_t *r);
el_err_t storage_log_get(uint32_t index, el_result_t *out);
/* Index of the first record with seq > `seq` (count if none), the record count, and the seq of
 * record 0 (record i has seq base_seq + i). Outputs other than index may be NULL. */
el_err_t storage_log_first_after(uint32_t seq, uint32_t *index, uint32_t *count, uint32_t *base_seq);
/* Wi-Fi upload watermark: last seq the server accepted. Persisted (two-slot NVS); RAM is
 * updated even when the NVS write fails (then a power cut may resend a batch: duplicates are
 * harmless, the server sees the same "record" numbers). */
uint32_t storage_uplink_wm(void);
el_err_t storage_uplink_wm_set(uint32_t seq);
/* Slow (reads flash): never call from the LVGL task for more than one screen page. */
el_err_t storage_log_iterate(uint32_t start, uint32_t limit, el_datalog_dir_t dir,
                             el_datalog_cb_t cb, void *ctx);
el_err_t storage_log_delete_all(el_level_t who);                /* ADMIN */
void     storage_log_status(uint32_t *count, uint8_t *fill_pct, bool *near_full, bool *full);

/* ---- event / diagnostic log (SD card if present, else a small ring in internal flash) ---- */
void storage_event_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* ---- export to the SD card (own task; EVT_EXPORT_PROGRESS / EVT_EXPORT_DONE) ---- */
typedef enum { STORAGE_EXPORT_ALL = 0, STORAGE_EXPORT_SINCE_LAST } storage_export_scope_t;
esp_err_t storage_export_start(storage_export_scope_t scope);
bool      storage_export_busy(void);

/* ---- SD card ---- */
bool      storage_sd_present(void);
esp_err_t storage_sd_space(uint64_t *free_bytes, uint64_t *total_bytes);

/* ======================================================================================
 * Additions by the storage service (docs/STORAGE.md). Nothing above this line changed.
 * ====================================================================================== */

/* Boot details that do not fit storage_boot_report_t. */
typedef struct {
    bool     fat_formatted;      /* internal FAT could not be mounted at all and was reformatted
                                    (first boot of a new board, or corruption): logged as an event */
    bool     log_quarantined;    /* /data/log.bin was not a result log: renamed to log_bad.bin */
    bool     log_newer_version;  /* log written by newer firmware: left untouched, log disabled */
    bool     nvs_init_failed;    /* NVS would not initialise: NOT erased; running on defaults */
    uint32_t log_capacity;       /* records the FAT can hold (target >= 50,000) */
    uint64_t fat_total_bytes, fat_free_bytes;
} storage_boot_details_t;
void storage_boot_details(storage_boot_details_t *out);

/* Internal FAT space (/data). ESP_ERR_INVALID_STATE if not mounted. */
esp_err_t storage_internal_space(uint64_t *free_bytes, uint64_t *total_bytes);
uint32_t  storage_log_capacity(void);

/* ENGINEER recovery for "NVS would not initialise": erase the NVS partition and rewrite every
 * blob from what is in RAM. EL_ERR_DENIED / EL_ERR_IO. Logged. */
el_err_t  storage_nvs_rebuild(el_level_t who);

/* Same as storage_event_log() with a va_list. Non-blocking: queued to the storage task. */
void      storage_event_logv(const char *fmt, va_list ap);
/* Wait (up to timeout_ms) until every queued event line is on flash. For "log, then reboot". */
void      storage_event_flush(uint32_t timeout_ms);

/* SD card / mirror state for the Diagnostics screen. */
typedef struct {
    bool     present;
    bool     mirror_behind;      /* results not yet on the card (absent, error, or catching up) */
    uint32_t mirror_seq;         /* last result seq written to the card */
    uint32_t mirror_errors;      /* write errors since boot */
    uint32_t events_dropped;     /* event lines lost because the queue was full */
} storage_sd_status_t;
void      storage_sd_status(storage_sd_status_t *out);

/* Outcome of the last export (valid once EVT_EXPORT_DONE was posted). */
typedef struct {
    el_err_t err;                /* EL_OK; EL_ERR_IO medium removed / write failed; EL_ERR_SIZE no
                                    room on the medium; EL_ERR_FORMAT log cleared during export */
    uint32_t rows;
    bool     nothing_to_export;  /* scope selected no rows: no file was created */
    char     file[64];           /* final file name (no directory), "" if none */
} storage_export_result_t;
void      storage_export_last(storage_export_result_t *out);

/* Self-tests: NVS (BOOT), STORAGE (BOOT), LOG_VERIFY (FULL, reads the whole log). */
const el_test_def_t *storage_selftests(size_t *n);
