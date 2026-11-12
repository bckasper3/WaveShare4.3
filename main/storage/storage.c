/* Storage service: init, NVS-backed configuration, result log API, self-tests.
 * Power can vanish between any two lines (docs/HARDWARE.md 12): every persisted change goes
 * through a two-slot NVS blob or the crash-safe datalog, and RAM is updated only after the
 * write is durable, so RAM never claims something flash does not have. */
#include <math.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "app_events.h"
#include "storage_priv.h"
#include "el_time.h"

static const char *TAG = "storage";

stg_t g_stg;

void stg_lock(void)   { xSemaphoreTakeRecursive(g_stg.mtx, portMAX_DELAY); }
void stg_unlock(void) { xSemaphoreGiveRecursive(g_stg.mtx); }

/* ---------------- codec accept callbacks (decode into static scratch) ---------------- */

static bool accept_settings(const uint8_t *p, size_t n, void *ctx)
{
    return el_settings_decode(p, n, (el_settings_t *)ctx) == EL_OK;
}

/* Scratch for encode/decode: static so nothing big lands on a caller's stack. Lock held. */
static uint8_t     s_blob[EL_SETTINGS_ENCODED_MAX];

static void load_blobs(storage_boot_report_t *rep)
{
    size_t n;
    el_settings_report_t srep = { 0 };
    el_err_t e = stg_nvs_load(STG_BLOB_SETTINGS, s_blob, sizeof s_blob, &n, accept_settings,
                              &g_stg.settings);
    if (e == EL_OK) e = el_settings_decode_ex(s_blob, n, &g_stg.settings, &srep);
    if (e != EL_OK) el_settings_defaults(&g_stg.settings);
    rep->settings_defaulted = (e != EL_OK) || srep.defaulted > 0;
    if (e == EL_OK && srep.defaulted)
        storage_event_log("nvs: %u stored settings out of range, defaulted", (unsigned)srep.defaulted);

    if (stg_nvs_load_u32(STG_BLOB_EXPORT_WM, &g_stg.export_wm) != EL_OK) g_stg.export_wm = 0;
    if (stg_nvs_load_u32(STG_BLOB_UPLINK_WM, &g_stg.uplink_wm) != EL_OK) g_stg.uplink_wm = 0;
}


static void load_defaults(void)
{
    el_settings_defaults(&g_stg.settings);
}

static const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    case ESP_RST_USB:       return "USB";
    case ESP_RST_JTAG:      return "JTAG";
    default:                return "UNKNOWN";
    }
}

static void log_boot(const storage_boot_report_t *r)
{
    const el_datalog_report_t *l = &r->log_recovery;
    storage_event_log("boot: reset=%s fw=%s nvs=%d fat=%d log=%d sd=%d records=%lu next_seq=%lu",
                      reset_reason_name(esp_reset_reason()), esp_app_get_description()->version,
                      r->nvs_ok, r->fat_ok, r->log_ok, r->sd_present,
                      (unsigned long)l->records, (unsigned long)l->next_seq);
    if (l->bytes_truncated || l->header_recreated || l->header_repaired || l->torn_record_removed ||
        l->last_record_damaged || l->delete_completed)
        storage_event_log("log recovery: truncated=%lu B hdr_new=%d hdr_fix=%d torn=%d damaged_last=%d "
                          "delete_finished=%d", (unsigned long)l->bytes_truncated, l->header_recreated,
                          l->header_repaired, l->torn_record_removed, l->last_record_damaged,
                          l->delete_completed);
    if (g_stg.det.fat_formatted)
        storage_event_log("FAT: internal storage was not mountable and has been FORMATTED "
                          "(first boot of this board, or corruption)");
    if (g_stg.det.log_quarantined) storage_event_log("log: foreign log.bin kept as log_bad.bin, new log");
    if (g_stg.det.log_newer_version) storage_event_log("log: written by newer firmware: logging disabled");
    if (g_stg.det.nvs_init_failed) storage_event_log("NVS: init failed, NOT erased, running on defaults");
    if (r->settings_defaulted) storage_event_log("settings: defaults in use");
}

esp_err_t storage_init(storage_boot_report_t *report)
{
    static storage_boot_report_t dummy;
    storage_boot_report_t *rep = report ? report : &dummy;
    memset(rep, 0, sizeof *rep);
    if (!g_stg.mtx) g_stg.mtx = xSemaphoreCreateRecursiveMutex();
    stg_events_early_init();

    stg_lock();
    load_defaults();
    g_stg.nvs_ok = (stg_nvs_open() == ESP_OK);
    g_stg.det.nvs_init_failed = !g_stg.nvs_ok;
    if (g_stg.nvs_ok) load_blobs(rep);
    else rep->settings_defaulted = true;
    rep->nvs_ok = g_stg.nvs_ok;

    g_stg.fat_ok = (stg_fat_mount(&g_stg.det.fat_formatted) == ESP_OK);
    rep->fat_ok = g_stg.fat_ok;
    if (g_stg.fat_ok) g_stg.log_ok = (stg_log_open(rep) == EL_OK);
    rep->log_ok = g_stg.log_ok;

    uint32_t mir = 0;
    if (g_stg.nvs_ok && stg_nvs_load_u32(STG_BLOB_MIRROR_WM, &mir) != EL_OK) mir = 0;
    el_store_mirror_init(&g_stg.mirror, mir, (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
    stg_unlock();

    rep->sd_present = (sd_card_mount(STORAGE_SD_MOUNT) == ESP_OK);
    stg_publish_log_state();
    stg_task_start();
    log_boot(rep);
    ESP_LOGI(TAG, "nvs=%d fat=%d log=%d (%lu/%lu records) sd=%d", rep->nvs_ok, rep->fat_ok,
             rep->log_ok, (unsigned long)rep->log_recovery.records,
             (unsigned long)g_stg.det.log_capacity, rep->sd_present);
    return ESP_OK;      /* never fails hard: the report says what is broken */
}

void storage_boot_details(storage_boot_details_t *out)
{
    if (!out) return;
    stg_lock();
    *out = g_stg.det;
    stg_unlock();
}

/* ---------------- settings ---------------- */

void storage_settings_get(el_settings_t *out)
{
    if (!out) return;
    stg_lock();
    *out = g_stg.settings;
    stg_unlock();
}

static el_err_t save_settings(const el_settings_t *s)
{
    size_t n;
    el_err_t e = el_settings_encode(s, s_blob, sizeof s_blob, &n);
    return e == EL_OK ? stg_nvs_save(STG_BLOB_SETTINGS, s_blob, n) : e;
}

el_err_t storage_settings_set(el_set_id_t id, const char *text, el_level_t who)
{
    const el_set_def_t *def = el_settings_def(id);
    if (!def) return EL_ERR_ARG;
    char old_txt[24], new_txt[24];
    stg_lock();
    el_settings_t next = g_stg.settings;
    el_err_t e = el_settings_set_str(&next, id, text, who);
    if (e == EL_OK) e = g_stg.nvs_ok ? save_settings(&next) : EL_ERR_IO;
    if (e == EL_OK) {
        el_settings_get_str(&g_stg.settings, id, old_txt, sizeof old_txt);
        el_settings_get_str(&next, id, new_txt, sizeof new_txt);
        g_stg.settings = next;
    }
    stg_unlock();
    if (e != EL_OK) return e;
    /* Secret settings print as "****" (el_settings_get_str), so the PIN never reaches a file. */
    storage_event_log("setting %s: '%s' -> '%s' by %s", def->key, old_txt, new_txt, el_level_name(who));
    app_events_post(EVT_SETTINGS_CHANGED, SRC_INTERNAL, (int32_t)id);
    return EL_OK;
}

el_err_t storage_settings_factory_reset(el_level_t who)
{
    if (!el_access_allows(who, EL_ACT_FACTORY_RESET)) return EL_ERR_DENIED;
    stg_lock();
    el_settings_t next;
    el_settings_defaults(&next);
    /* Identity is provisioning, not a setting: a factory reset keeps the serial number. */
    memcpy(next.device_serial, g_stg.settings.device_serial, sizeof next.device_serial);
    el_err_t e = g_stg.nvs_ok ? save_settings(&next) : EL_ERR_IO;
    if (e == EL_OK) g_stg.settings = next;
    stg_unlock();
    if (e != EL_OK) return e;
    storage_event_log("settings: factory reset by %s (serial kept)", el_level_name(who));
    app_events_post(EVT_SETTINGS_CHANGED, SRC_INTERNAL, -1);
    return EL_OK;
}

/* ---------------- result log ---------------- */

el_err_t storage_log_append(el_result_t *r)
{
    if (!r) return EL_ERR_ARG;
    uint32_t seq = 0;
    stg_lock();
    el_err_t e = g_stg.log_ok ? el_datalog_append(&g_stg.log, r, &seq) : EL_ERR_IO;
    if (e == EL_OK) r->seq = seq;
    stg_unlock();
    if (e != EL_OK) {
        storage_event_log("log append failed: %s", el_err_name(e));
        return e;
    }
    stg_publish_log_state();
    stg_task_kick();                     /* SD mirror: done by the storage task, never here */
    app_events_post(EVT_LOG_CHANGED, SRC_INTERNAL, 0);
    return EL_OK;
}

el_err_t storage_log_first_after(uint32_t seq, uint32_t *index, uint32_t *count, uint32_t *base_seq)
{
    stg_lock();
    el_err_t e = EL_ERR_IO;
    if (g_stg.log_ok) {
        e = el_datalog_find_first_after_seq(&g_stg.log, seq, index);
        if (count) *count = el_datalog_count(&g_stg.log);
        if (base_seq) *base_seq = g_stg.log.base_seq;
    }
    stg_unlock();
    return e;
}

uint32_t storage_uplink_wm(void)
{
    stg_lock();
    uint32_t v = g_stg.uplink_wm;
    stg_unlock();
    return v;
}

el_err_t storage_uplink_wm_set(uint32_t seq)
{
    stg_lock();
    g_stg.uplink_wm = seq;
    stg_unlock();
    return stg_nvs_save_u32(STG_BLOB_UPLINK_WM, seq);
}

el_err_t storage_log_get(uint32_t index, el_result_t *out)
{
    stg_lock();
    el_err_t e = g_stg.log_ok ? el_datalog_get(&g_stg.log, index, out) : EL_ERR_IO;
    stg_unlock();
    return e;
}

el_err_t storage_log_iterate(uint32_t start, uint32_t limit, el_datalog_dir_t dir,
                             el_datalog_cb_t cb, void *ctx)
{
    stg_lock();
    el_err_t e = g_stg.log_ok ? el_datalog_iterate(&g_stg.log, start, limit, dir, cb, ctx, NULL)
                              : EL_ERR_IO;
    stg_unlock();
    return e;
}

el_err_t storage_log_delete_all(el_level_t who)
{
    if (!el_access_allows(who, EL_ACT_DELETE_LOG)) return EL_ERR_DENIED;
    stg_lock();
    uint32_t n = el_datalog_count(&g_stg.log);
    uint32_t first = el_datalog_first_seq(&g_stg.log);
    el_err_t e = g_stg.log_ok ? el_datalog_delete_all(&g_stg.log, (int64_t)time(NULL)) : EL_ERR_IO;
    stg_unlock();
    if (e != EL_OK) return e;
    storage_event_log("log: %lu results (seq %lu..%lu) deleted by %s", (unsigned long)n,
                      (unsigned long)first, (unsigned long)(first + n - (n ? 1u : 0u)),
                      el_level_name(who));
    stg_publish_log_state();
    app_events_post(EVT_LOG_CHANGED, SRC_INTERNAL, 0);
    return EL_OK;
}

void storage_log_status(uint32_t *count, uint8_t *fill_pct, bool *near_full, bool *full)
{
    stg_lock();
    if (count) *count = el_datalog_count(&g_stg.log);
    if (fill_pct) *fill_pct = (uint8_t)el_datalog_fill_pct(&g_stg.log);
    if (near_full) *near_full = el_datalog_near_full(&g_stg.log);
    if (full) *full = el_datalog_full(&g_stg.log);
    stg_unlock();
}

uint32_t storage_log_capacity(void)
{
    stg_lock();
    uint32_t c = el_datalog_capacity(&g_stg.log);
    stg_unlock();
    return c;
}

/* ---------------- NVS recovery ---------------- */

el_err_t storage_nvs_rebuild(el_level_t who)
{
    if (who != EL_LEVEL_ENGINEER) return EL_ERR_DENIED;
    stg_lock();
    el_err_t e = (stg_nvs_erase_and_reopen() == ESP_OK) ? EL_OK : EL_ERR_IO;
    g_stg.nvs_ok = (e == EL_OK);
    size_t n;
    if (e == EL_OK) e = save_settings(&g_stg.settings);
    if (e == EL_OK) e = stg_nvs_save_u32(STG_BLOB_EXPORT_WM, g_stg.export_wm);
    if (e == EL_OK) e = stg_nvs_save_u32(STG_BLOB_MIRROR_WM, g_stg.mirror.done_seq);
    if (e == EL_OK) e = stg_nvs_save_u32(STG_BLOB_UPLINK_WM, g_stg.uplink_wm);
    (void)n;
    stg_unlock();
    storage_event_log("NVS: erased and rebuilt from RAM by %s: %s", el_level_name(who), el_err_name(e));
    return e;
}

/* ---------------- self-tests ---------------- */

static void test_nvs(void *ctx, el_test_report_t *rep)
{
    (void)ctx;
    if (!g_stg.nvs_ok) { el_test_set(rep, EL_TEST_FAIL, NAN, "NVS init failed"); return; }
    size_t used = 0, total = 0;
    if (stg_nvs_usage(&used, &total) != ESP_OK || total == 0) {
        el_test_set(rep, EL_TEST_FAIL, NAN, "nvs_get_stats failed");
        return;
    }
    float pct = 100.0f * (float)used / (float)total;
    storage_boot_details_t d;
    storage_boot_details(&d);
    el_settings_t s;
    storage_settings_get(&s);
    el_test_outcome_t o = pct > 90.0f ? EL_TEST_FAIL : (pct > 75.0f ? EL_TEST_WARN : EL_TEST_PASS);
    el_test_set(rep, o, pct, "%u/%u entries, settings %s", (unsigned)used, (unsigned)total,
                el_settings_validate(&s) == EL_OK ? "ok" : "BAD");
}

static void test_storage(void *ctx, el_test_report_t *rep)
{
    (void)ctx;
    if (!g_stg.fat_ok) { el_test_set(rep, EL_TEST_FAIL, NAN, "internal FAT not mounted"); return; }
    if (!g_stg.log_ok) { el_test_set(rep, EL_TEST_FAIL, NAN, "result log not open"); return; }
    uint64_t free_b = 0, total = 0;
    storage_internal_space(&free_b, &total);
    float free_kb = (float)free_b / 1024.0f;
    stg_lock();
    uint32_t n = el_datalog_count(&g_stg.log);
    el_err_t tail = EL_OK;
    if (n) {
        static el_result_t r;
        tail = el_datalog_get(&g_stg.log, n - 1, &r);
    }
    bool near = el_datalog_near_full(&g_stg.log);
    uint32_t pct = el_datalog_fill_pct(&g_stg.log);
    stg_unlock();
    if (tail == EL_ERR_IO) { el_test_set(rep, EL_TEST_FAIL, free_kb, "log tail read I/O error"); return; }
    el_test_outcome_t o = (tail != EL_OK || near) ? EL_TEST_WARN : EL_TEST_PASS;
    el_test_set(rep, o, free_kb, "%lu results, log %lu%% full, tail %s", (unsigned long)n,
                (unsigned long)pct, tail == EL_OK ? "ok" : el_err_name(tail));
}

static void test_log_verify(void *ctx, el_test_report_t *rep)
{
    (void)ctx;
    if (!g_stg.log_ok) { el_test_set(rep, EL_TEST_FAIL, NAN, "result log not open"); return; }
    el_datalog_verify_t v;
    stg_lock();
    el_err_t e = el_datalog_verify_all(&g_stg.log, &v);
    stg_unlock();
    if (e != EL_OK) { el_test_set(rep, EL_TEST_FAIL, NAN, "verify: %s", el_err_name(e)); return; }
    if (v.bad) {
        el_test_set(rep, EL_TEST_WARN, (float)v.bad, "%lu good, %lu damaged, first at %lu",
                    (unsigned long)v.good, (unsigned long)v.bad, (unsigned long)v.first_bad_index);
        return;
    }
    el_test_set(rep, EL_TEST_PASS, 0.0f, "%lu records verified", (unsigned long)v.good);
}

static const el_test_def_t s_tests[] = {
    { "NVS",        "NVS opens, blobs decode with CRC",          "%",  NULL,      EL_TG_BOOT, test_nvs },
    { "STORAGE",    "FAT mounted, log tail valid, free space",   "kB", NULL,      EL_TG_BOOT, test_storage },
    { "LOG_VERIFY", "Read and check every log record",           "",   "STORAGE", EL_TG_FULL, test_log_verify },
};

const el_test_def_t *storage_selftests(size_t *n)
{
    if (n) *n = sizeof s_tests / sizeof s_tests[0];
    return s_tests;
}
