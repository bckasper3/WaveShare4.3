/* Storage task: event/diagnostic log, SD card presence, SD CSV mirror (docs/STORAGE.md 5-6).
 * Bookkeeping is el_store (host-tested): event-line format and rotation, mirror catch-up by
 * seq, watermark save interval. Nothing here ever blocks a caller: event lines are queued,
 * mirroring happens on this task. */
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "storage_priv.h"
#include "freertos/queue.h"
#include "app_events.h"
#include "fw_update.h"
#include "el_time.h"

static const char *TAG = "stg_sd";

typedef struct {
    int64_t  unix_s;
    uint32_t up_ms;
    char     msg[STG_EV_MSG_LEN];
} stg_ev_t;

static QueueHandle_t s_evq;
static volatile uint32_t s_ev_queued, s_ev_done, s_ev_dropped;
static bool s_sd_present;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ---------------- event log ---------------- */

void stg_events_early_init(void)
{
    if (!s_evq) s_evq = xQueueCreate(48, sizeof(stg_ev_t));   /* boot lines wait for the task */
}

void storage_event_logv(const char *fmt, va_list ap)
{
    if (!s_evq || !fmt) return;
    stg_ev_t ev;
    ev.unix_s = (int64_t)time(NULL);
    ev.up_ms = now_ms();
    vsnprintf(ev.msg, sizeof ev.msg, fmt, ap);
    if (xQueueSend(s_evq, &ev, 0) == pdTRUE) {
        s_ev_queued++;
        stg_task_kick();
    } else {
        s_ev_dropped++;
    }
}

void storage_event_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    storage_event_logv(fmt, ap);
    va_end(ap);
}

void storage_event_flush(uint32_t timeout_ms)
{
    stg_task_kick();
    uint32_t t0 = now_ms();
    while (s_ev_done != s_ev_queued && now_ms() - t0 < timeout_ms) vTaskDelay(pdMS_TO_TICKS(10));
}

static long file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : 0;
}

/* Append every queued line to the active event file (SD if present, else the internal ring). */
static void drain_events(void)
{
    stg_ev_t ev;
    if (uxQueueMessagesWaiting(s_evq) == 0) return;
    bool sd = s_sd_present;
    if (!sd && !g_stg.fat_ok) {                     /* nowhere to write: drop, but count them done */
        while (xQueueReceive(s_evq, &ev, 0) == pdTRUE) s_ev_done++;
        return;
    }
    const char *cur = sd ? STG_SD_EV_PATH : STG_EV_PATH;
    const char *old = sd ? STG_SD_EV_OLD_PATH : STG_EV_OLD_PATH;
    uint32_t limit = sd ? STG_SD_EV_LIMIT : STG_EV_LIMIT;
    uint32_t size = (uint32_t)file_size(cur);
    FILE *f = NULL;
    char line[EL_STORE_EVLINE_MAX];
    while (xQueueReceive(s_evq, &ev, 0) == pdTRUE) {
        size_t n = el_store_evline_format(line, sizeof line, ev.unix_s, ev.up_ms, ev.msg);
        if (el_store_evlog_rotate_due(size, (uint32_t)n, limit)) {
            if (f) { fclose(f); f = NULL; }
            remove(old);
            rename(cur, old);
            size = 0;
        }
        if (!f) f = fopen(cur, "a");
        if (f && fwrite(line, 1, n, f) == n) size += (uint32_t)n;
        s_ev_done++;
    }
    if (f) fclose(f);
}

/* ---------------- SD presence + mirror ---------------- */

void stg_csv_ctx(el_csv_ctx_t *ctx, char serial[EL_SET_SERIAL_MAX + 1])
{
    stg_lock();
    memcpy(serial, g_stg.settings.device_serial, EL_SET_SERIAL_MAX + 1);
    serial[EL_SET_SERIAL_MAX] = '\0';
    unsigned dec = g_stg.settings.decimals;
    stg_unlock();
    ctx->device_serial = serial;
    ctx->fw_version = fw_update_running_version();
    ctx->decimals = dec;
}

/* Write one result to its day file. false on any write error. */
static bool mirror_write(const el_result_t *r, const el_csv_ctx_t *ctx, const char *serial)
{
    char name[EL_STORE_NAME_MAX], path[sizeof STORAGE_SD_MOUNT + EL_STORE_NAME_MAX + 2];
    static char row[EL_CSV_ROW_MAX + 2];
    if (el_store_mirror_name(name, sizeof name, serial, r->timestamp_s) != EL_OK) return false;
    snprintf(path, sizeof path, "%s/%s", STORAGE_SD_MOUNT, name);
    bool fresh = file_size(path) == 0;
    FILE *f = fopen(path, "a");
    if (!f) return false;
    bool ok = true;
    if (fresh) ok = fprintf(f, "%s\n", el_csv_header()) > 0;
    if (ok && el_csv_format_result(row, EL_CSV_ROW_MAX, r, ctx) == EL_OK) {
        strcat(row, "\n");
        ok = fputs(row, f) >= 0;
    }
    ok = (fclose(f) == 0) && ok;
    return ok;
}

static void mirror_step(void)
{
    if (!s_sd_present || !g_stg.log_ok) return;
    static el_result_t batch[STG_MIRROR_BATCH];
    static bool have[STG_MIRROR_BATCH];
    uint32_t from = 0, n = 0;

    stg_lock();
    uint32_t first = el_datalog_first_seq(&g_stg.log), next = el_datalog_next_seq(&g_stg.log);
    bool more = el_store_mirror_next(&g_stg.mirror, first, next, STG_MIRROR_BATCH, &from, &n);
    for (uint32_t i = 0; more && i < n; i++)
        have[i] = el_datalog_get(&g_stg.log, from + i - first, &batch[i]) == EL_OK;  /* seq = base + index */
    stg_unlock();
    if (!more) return;

    char serial[EL_SET_SERIAL_MAX + 1];
    el_csv_ctx_t ctx;
    stg_csv_ctx(&ctx, serial);
    ctx.fw_version = fw_update_running_version();
    for (uint32_t i = 0; i < n; i++) {
        if (have[i] && !mirror_write(&batch[i], &ctx, serial)) {
            stg_lock();
            g_stg.mirror_errors++;
            stg_unlock();
            ESP_LOGW(TAG, "mirror write failed at seq %lu", (unsigned long)(from + i));
            return;                                  /* retry on the next pass */
        }
        stg_lock();
        el_store_mirror_done(&g_stg.mirror, from + i);   /* damaged records are skipped too */
        stg_unlock();
    }
}

static void mirror_save_if_due(void)
{
    uint32_t now = now_ms();
    stg_lock();
    bool due = g_stg.nvs_ok && el_store_mirror_save_due(&g_stg.mirror, now, STG_MIRROR_SAVE_MS);
    uint32_t done = g_stg.mirror.done_seq;
    stg_unlock();
    if (!due) return;
    if (stg_nvs_save_u32(STG_BLOB_MIRROR_WM, done) == EL_OK) {
        stg_lock();
        el_store_mirror_saved(&g_stg.mirror, now);
        stg_unlock();
    }
}

static void sd_poll(void)
{
    bool changed = sd_card_poll();
    s_sd_present = sd_card_mounted();
    if (!changed) return;
    ESP_LOGI(TAG, "SD card %s", s_sd_present ? "inserted" : "removed");
    app_events_post(s_sd_present ? EVT_SD_MOUNTED : EVT_SD_REMOVED, SRC_INTERNAL, 0);
    storage_event_log("SD card %s", s_sd_present ? "mounted" : "removed");
}

static void storage_task(void *arg)
{
    (void)arg;
    uint32_t last_poll = 0;
    s_sd_present = sd_card_mounted();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(STG_TASK_PERIOD_MS));
        uint32_t now = now_ms();
        if (now - last_poll >= STG_SD_POLL_MS) {
            last_poll = now;
            sd_poll();
        }
        drain_events();
        mirror_step();
        mirror_save_if_due();
    }
}

void stg_task_start(void)
{
    if (g_stg.task) return;
    stg_events_early_init();
    xTaskCreatePinnedToCore(storage_task, "storage", 6144, NULL, 2, &g_stg.task, 0);
}

void stg_task_kick(void)
{
    if (g_stg.task) xTaskNotifyGive(g_stg.task);
}

/* ---------------- public SD queries ---------------- */

bool storage_sd_present(void) { return sd_card_mounted(); }

esp_err_t storage_sd_space(uint64_t *free_bytes, uint64_t *total_bytes)
{
    return sd_card_space(free_bytes, total_bytes);
}

void storage_sd_status(storage_sd_status_t *out)
{
    if (!out) return;
    bool present = sd_card_mounted();
    stg_lock();
    uint32_t first = g_stg.log_ok ? el_datalog_first_seq(&g_stg.log) : 0;
    uint32_t next = g_stg.log_ok ? el_datalog_next_seq(&g_stg.log) : 0;
    out->present = present;
    out->mirror_behind = !present || el_store_mirror_behind(&g_stg.mirror, first, next);
    out->mirror_seq = g_stg.mirror.done_seq;
    out->mirror_errors = g_stg.mirror_errors;
    stg_unlock();
    out->events_dropped = s_ev_dropped;
}
