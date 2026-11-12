/* CSV export to the SD card (docs/STORAGE.md section 7), in its own task. Range, file name and
 * watermark rules are el_store (host-tested). The file is written under a temporary name and
 * renamed into place; only then does the watermark move, so a pulled card or power cut never
 * marks rows as exported that are not in a finished file. */
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "esp_log.h"
#include "storage_priv.h"
#include "fw_update.h"
#include "app_state.h"
#include "app_events.h"

static const char *TAG = "export";
#define CHUNK        16u
#define ROW_ESTIMATE 220u          /* bytes per CSV row, for the free-space check */

static volatile bool s_running;
static storage_export_result_t s_last;
static struct {
    el_store_exp_scope_t    scope;
} s_job;

bool storage_export_busy(void) { return s_running; }
bool stg_export_running(void)  { return s_running; }

void storage_export_last(storage_export_result_t *out)
{
    if (!out) return;
    stg_lock();
    *out = s_last;
    stg_unlock();
}

static void set_busy(bool b, uint8_t pct)
{
    app_state_lock();
    app_state_raw()->export_busy = b;
    app_state_raw()->export_progress_pct = pct;
    app_state_unlock();
}

static bool exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static el_err_t run(storage_export_result_t *res)
{
    const char *dir = STORAGE_SD_MOUNT;
    if (!sd_card_mounted() || !g_stg.log_ok) return EL_ERR_IO;

    stg_lock();
    uint32_t first = el_datalog_first_seq(&g_stg.log), next = el_datalog_next_seq(&g_stg.log);
    uint32_t wm = g_stg.export_wm;
    stg_unlock();

    uint32_t from = 0, end = 0;
    if (el_store_export_range(s_job.scope, wm, first, next, &from, &end) != EL_OK) return EL_ERR_ARG;
    if (from == end) { res->nothing_to_export = true; return EL_OK; }

    uint64_t free_b = 0, total_b = 0;
    esp_err_t se = sd_card_space(&free_b, &total_b);
    if (se == ESP_OK && free_b < (uint64_t)(end - from) * ROW_ESTIMATE + 4096u) return EL_ERR_SIZE;

    char serial[EL_SET_SERIAL_MAX + 1];
    el_csv_ctx_t ctx;
    stg_csv_ctx(&ctx, serial);

    char name[EL_STORE_NAME_MAX], path[96], tmp[96];
    int64_t now = (int64_t)time(NULL);
    unsigned n = 0;
    do {
        if (el_store_export_name(name, sizeof name, serial, now, n++) != EL_OK) return EL_ERR_ARG;
        snprintf(path, sizeof path, "%s/%s", dir, name);
    } while (exists(path) && n < 100);
    snprintf(tmp, sizeof tmp, "%s/ENVLOG_EXPORT.TMP", dir);

    FILE *f = fopen(tmp, "w");
    if (!f) return EL_ERR_IO;
    static char row[EL_CSV_ROW_MAX + 2];
    static el_result_t chunk[CHUNK];
    static bool have[CHUNK];
    el_err_t err = fprintf(f, "%s\n", el_csv_header()) > 0 ? EL_OK : EL_ERR_IO;
    uint8_t last_pct = 0;

    for (uint32_t seq = from; seq < end && err == EL_OK; seq += CHUNK) {
        uint32_t cnt = end - seq < CHUNK ? end - seq : CHUNK;
        stg_lock();
        uint32_t f_now = el_datalog_first_seq(&g_stg.log);
        if (f_now > seq || el_datalog_next_seq(&g_stg.log) < end) err = EL_ERR_FORMAT;   /* log cleared */
        for (uint32_t i = 0; err == EL_OK && i < cnt; i++)
            have[i] = el_datalog_get(&g_stg.log, seq + i - f_now, &chunk[i]) == EL_OK;
        stg_unlock();

        for (uint32_t i = 0; err == EL_OK && i < cnt; i++) {
            if (!have[i]) continue;
            if (el_csv_format_result(row, EL_CSV_ROW_MAX, &chunk[i], &ctx) != EL_OK) continue;
            strcat(row, "\n");
            if (fputs(row, f) < 0) err = errno == ENOSPC ? EL_ERR_SIZE : EL_ERR_IO;
            else res->rows++;
        }
        uint8_t pct = el_store_pct(seq + cnt - from, end - from);
        if (pct >= last_pct + 5 || pct == 100) {
            last_pct = pct;
            set_busy(true, pct);
            app_events_post(EVT_EXPORT_PROGRESS, SRC_INTERNAL, pct);
        }
    }
    if (fflush(f) != 0 && err == EL_OK) err = errno == ENOSPC ? EL_ERR_SIZE : EL_ERR_IO;
    if (fclose(f) != 0 && err == EL_OK) err = EL_ERR_IO;
    if (err != EL_OK) { remove(tmp); return err; }
    if (rename(tmp, path) != 0) { remove(tmp); return EL_ERR_IO; }
    snprintf(res->file, sizeof res->file, "%s", name);

    /* The file is complete and in place: now the watermark may move. */
    uint32_t nwm = el_store_export_watermark(s_job.scope, wm, end);
    if (nwm != wm) {
        stg_lock();
        g_stg.export_wm = nwm;
        stg_unlock();
        if (stg_nvs_save_u32(STG_BLOB_EXPORT_WM, nwm) != EL_OK)
            storage_event_log("export: watermark not saved (next SINCE_LAST repeats rows)");
    }
    return EL_OK;
}

static void export_task(void *arg)
{
    (void)arg;
    storage_export_result_t res = { 0 };
    set_busy(true, 0);
    app_events_post(EVT_EXPORT_PROGRESS, SRC_INTERNAL, 0);
    res.err = run(&res);
    stg_lock();
    s_last = res;
    stg_unlock();
    storage_event_log("export SD scope=%d: %s, %lu rows%s%s", s_job.scope, el_err_name(res.err), (unsigned long)res.rows, res.file[0] ? " -> " : "",
                      res.nothing_to_export ? "nothing to export" : res.file);
    ESP_LOGI(TAG, "done: %s, %lu rows %s", el_err_name(res.err), (unsigned long)res.rows, res.file);
    set_busy(false, 100);
    s_running = false;
    app_events_post(EVT_EXPORT_DONE, SRC_INTERNAL, (int32_t)res.err);
    vTaskDelete(NULL);
}

esp_err_t storage_export_start(storage_export_scope_t scope)
{
    if (s_running) return ESP_ERR_INVALID_STATE;
    if (scope > STORAGE_EXPORT_SINCE_LAST) return ESP_ERR_INVALID_ARG;
    s_job.scope = (el_store_exp_scope_t)scope;
    s_running = true;
    if (xTaskCreatePinnedToCore(export_task, "export", 6144, NULL, 2, NULL, 0) != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
