#pragma once
/* Private to main/storage/: state shared by the storage source files. Nothing outside this
 * folder may include it. Every field is guarded by stg_lock() unless noted. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "storage.h"
#include "sd_card.h"
#include "el_store.h"
#include "el_csv.h"

#define STG_PARTITION        "storage"
#define STG_LOG_PATH         STORAGE_MOUNT "/log.bin"
#define STG_LOG_BAD_PATH     STORAGE_MOUNT "/log_bad.bin"
#define STG_EV_PATH          STORAGE_MOUNT "/events.log"
#define STG_EV_OLD_PATH      STORAGE_MOUNT "/events.1.log"
#define STG_EV_LIMIT         (32u * 1024u)       /* internal ring: 2 x 32 kB */
#define STG_SD_EV_PATH       STORAGE_SD_MOUNT "/ENVLOG_EVENTS.log"
#define STG_SD_EV_OLD_PATH   STORAGE_SD_MOUNT "/ENVLOG_EVENTS.1.log"
#define STG_SD_EV_LIMIT      (1024u * 1024u)     /* SD: 2 x 1 MB */
#define STG_FAT_RESERVE      (512u * 1024u)      /* kept free for event files, renames, FAT slack */
#define STG_SD_POLL_MS       3000u
#define STG_MIRROR_SAVE_MS   30000u
#define STG_MIRROR_BATCH     16u
#define STG_TASK_PERIOD_MS   500u
#define STG_EV_MSG_LEN       160

typedef struct {
    SemaphoreHandle_t mtx;         /* recursive: log iteration callbacks may call storage_log_get */
    bool  nvs_ok, fat_ok, log_ok;
    storage_boot_details_t det;

    el_settings_t      settings;
    uint32_t           export_wm;  /* last exported seq */
    uint32_t           uplink_wm;  /* last seq delivered over Wi-Fi */

    el_datalog_t       log;
    FILE              *logf;

    el_store_mirror_t  mirror;
    uint32_t           mirror_errors;
    TaskHandle_t       task;
} stg_t;

extern stg_t g_stg;

void stg_lock(void);
void stg_unlock(void);

/* ---- NVS two-slot blobs (storage_nvs.c) ---- */
typedef enum {
    STG_BLOB_SETTINGS = 0, STG_BLOB_EXPORT_WM, STG_BLOB_MIRROR_WM, STG_BLOB_UPLINK_WM, STG_BLOB_COUNT
} stg_blob_t;

esp_err_t stg_nvs_open(void);                 /* nvs_flash_init (never erases) + namespace */
esp_err_t stg_nvs_erase_and_reopen(void);
/* Load the newest accepted slot's payload into out. EL_OK, EL_ERR_FORMAT (never stored),
 * EL_ERR_CRC (stored, no slot valid), EL_ERR_SIZE (payload larger than cap), EL_ERR_IO. */
el_err_t  stg_nvs_load(stg_blob_t b, uint8_t *out, size_t cap, size_t *len,
                       el_store_accept_fn accept, void *ctx);
/* Write the not-current slot, commit. EL_OK only when nvs_commit succeeded. */
el_err_t  stg_nvs_save(stg_blob_t b, const void *payload, size_t len);
el_err_t  stg_nvs_save_u32(stg_blob_t b, uint32_t v);
el_err_t  stg_nvs_load_u32(stg_blob_t b, uint32_t *v);
/* used / total entries of the NVS partition, for the NVS self-test */
esp_err_t stg_nvs_usage(size_t *used, size_t *total);

/* ---- internal FAT + result log (storage_fat.c) ---- */
esp_err_t stg_fat_mount(bool *formatted);
el_err_t  stg_log_open(storage_boot_report_t *rep);
void      stg_publish_log_state(void);        /* app_state.log_* (call without the lock) */

/* ---- SD mirror, event log, storage task (storage_sd.c) ---- */
void      stg_events_early_init(void);
void      stg_task_start(void);
void      stg_task_kick(void);                /* new result: mirror soon */
/* CSV context from the current settings. The serial is copied into the caller's buffer, so
 * ctx stays valid after the lock is released. Takes the lock. */
void      stg_csv_ctx(el_csv_ctx_t *ctx, char serial[EL_SET_SERIAL_MAX + 1]);

/* ---- export (storage_export.c) ---- */
bool      stg_export_running(void);
