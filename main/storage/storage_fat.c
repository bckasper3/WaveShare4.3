/* Internal FAT (wear levelling, 512-byte SAFE sectors: sdkconfig.defaults) and the
 * el_datalog_io_t backend on a FILE*. The I/O contract is in el_datalog.h / docs/DATALOG.md:
 * append / write_at / truncate return EL_OK only once durable (fflush + fsync). */
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "app_state.h"
#include "storage_priv.h"

static const char *TAG = "stg_fat";
static wl_handle_t s_wl = WL_INVALID_HANDLE;

/* ---------------- mount ---------------- */

static esp_err_t mount(bool format)
{
    const esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed = format,
        .max_files = 8,                  /* log, events, mirror read-back, export tmp, spare */
        /* 1 kB clusters keep the volume FAT16 (~7,900 clusters). FAT12 (>= 4 kB clusters here)
         * packs entries in 12 bits that can straddle two 512-byte sectors, so one FAT update
         * could be torn across two sector writes. */
        .allocation_unit_size = 1024,
        .disk_status_check_enable = false,
        .use_one_fat = true,             /* FatFS never repairs from the 2nd copy: just writes */
    };
    return esp_vfs_fat_spiflash_mount_rw_wl(STORAGE_MOUNT, STG_PARTITION, &cfg, &s_wl);
}

esp_err_t stg_fat_mount(bool *formatted)
{
    *formatted = false;
    esp_err_t e = mount(false);
    if (e == ESP_FAIL) {
        /* f_mount found no usable FAT (FR_NO_FILESYSTEM / FR_INT_ERR): a brand-new board or a
         * destroyed volume. This is the ONLY automatic format the firmware does, and it touches
         * the result-log partition only (never NVS). Logged as an event by storage_init(). */
        ESP_LOGW(TAG, "FAT not mountable: formatting the '%s' partition", STG_PARTITION);
        e = mount(true);
        *formatted = (e == ESP_OK);
    }
    if (e != ESP_OK) ESP_LOGE(TAG, "mount %s: %s", STORAGE_MOUNT, esp_err_to_name(e));
    return e;
}

/* ---------------- el_datalog_io_t on FILE* ---------------- */

static el_err_t io_fail(const char *what)
{
    ESP_LOGE(TAG, "log %s: errno %d (%s)", what, errno, strerror(errno));
    return EL_ERR_IO;
}

static el_err_t io_size(void *ctx, uint32_t *out)
{
    FILE *f = ctx;
    struct stat st;
    if (fflush(f) != 0 || fstat(fileno(f), &st) != 0) return io_fail("size");
    if (st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) return EL_ERR_IO;
    *out = (uint32_t)st.st_size;
    return EL_OK;
}

static el_err_t io_read(void *ctx, uint32_t off, void *buf, uint32_t len)
{
    FILE *f = ctx;
    if (fseek(f, (long)off, SEEK_SET) != 0) return io_fail("seek");
    if (fread(buf, 1, len, f) != len) { clearerr(f); return io_fail("read"); }
    return EL_OK;
}

static el_err_t durable(FILE *f, const char *what)
{
    if (fflush(f) != 0 || fsync(fileno(f)) != 0) { clearerr(f); return io_fail(what); }
    return EL_OK;
}

static el_err_t io_append(void *ctx, const void *buf, uint32_t len)
{
    FILE *f = ctx;
    if (fseek(f, 0, SEEK_END) != 0) return io_fail("seek");
    if (fwrite(buf, 1, len, f) != len) { clearerr(f); return io_fail("append"); }
    return durable(f, "append sync");
}

static el_err_t io_write_at(void *ctx, uint32_t off, const void *buf, uint32_t len)
{
    FILE *f = ctx;
    if (fseek(f, (long)off, SEEK_SET) != 0) return io_fail("seek");
    if (fwrite(buf, 1, len, f) != len) { clearerr(f); return io_fail("write_at"); }
    return durable(f, "write_at sync");
}

static el_err_t io_truncate(void *ctx, uint32_t len)
{
    FILE *f = ctx;
    if (fflush(f) != 0) return io_fail("flush");
    if (ftruncate(fileno(f), (off_t)len) != 0) return io_fail("truncate");
    return durable(f, "truncate sync");
}

static FILE *open_log_file(void)
{
    FILE *f = fopen(STG_LOG_PATH, "r+b");
    if (!f && errno == ENOENT) f = fopen(STG_LOG_PATH, "w+b");   /* only if it does not exist */
    if (!f) ESP_LOGE(TAG, "open %s: errno %d", STG_LOG_PATH, errno);
    return f;
}

/* Capacity from the volume, not a constant: everything except a reserve for the event files
 * and FAT slack. ~58,000 records on the 8 MB partition (docs/STORAGE.md). */
static uint32_t compute_capacity(void)
{
    uint64_t total = 0, free_b = 0;
    if (esp_vfs_fat_info(STORAGE_MOUNT, &total, &free_b) != ESP_OK) return 50000u;
    g_stg.det.fat_total_bytes = total;
    g_stg.det.fat_free_bytes = free_b;
    if (total <= STG_FAT_RESERVE + EL_DATALOG_HEADER_SIZE + EL_RECORD_SIZE) return 1;
    uint64_t n = (total - STG_FAT_RESERVE - EL_DATALOG_HEADER_SIZE) / EL_RECORD_SIZE;
    if (n > EL_DATALOG_MAX_RECORDS_LIMIT) n = EL_DATALOG_MAX_RECORDS_LIMIT;
    return (uint32_t)n;
}

static el_err_t open_engine(storage_boot_report_t *rep)
{
    el_datalog_io_t io = {
        .ctx = g_stg.logf, .size = io_size, .read = io_read, .append = io_append,
        .write_at = io_write_at, .truncate = io_truncate,
    };
    return el_datalog_open(&g_stg.log, &io, g_stg.det.log_capacity, (int64_t)time(NULL),
                           &rep->log_recovery);
}

el_err_t stg_log_open(storage_boot_report_t *rep)
{
    g_stg.det.log_capacity = compute_capacity();
    g_stg.logf = open_log_file();
    if (!g_stg.logf) return EL_ERR_IO;
    el_err_t e = open_engine(rep);
    if (e == EL_ERR_FORMAT) {
        /* Not a result log (no valid header, no readable record). Keep it for the engineer
         * (log_bad.bin, replacing an older one) and start a new log: never delete data. */
        ESP_LOGE(TAG, "log.bin is not a result log: renaming to log_bad.bin");
        fclose(g_stg.logf);
        g_stg.logf = NULL;
        remove(STG_LOG_BAD_PATH);
        if (rename(STG_LOG_PATH, STG_LOG_BAD_PATH) != 0) return io_fail("quarantine");
        g_stg.det.log_quarantined = true;
        g_stg.logf = open_log_file();
        if (!g_stg.logf) return EL_ERR_IO;
        e = open_engine(rep);
    } else if (e == EL_ERR_VERSION) {
        g_stg.det.log_newer_version = true;      /* written by newer firmware: hands off */
    }
    if (e != EL_OK) {
        ESP_LOGE(TAG, "datalog open: %s", el_err_name(e));
        if (g_stg.logf) { fclose(g_stg.logf); g_stg.logf = NULL; }
    }
    return e;
}

void stg_publish_log_state(void)
{
    uint32_t count; uint8_t pct; bool near, full;
    storage_log_status(&count, &pct, &near, &full);
    app_state_lock();
    app_state_t *s = app_state_raw();
    s->log_count = count;
    s->log_fill_pct = pct;
    s->log_near_full = near;
    s->log_full = full;
    app_state_unlock();
}

esp_err_t storage_internal_space(uint64_t *free_bytes, uint64_t *total_bytes)
{
    if (!g_stg.fat_ok) return ESP_ERR_INVALID_STATE;
    uint64_t t = 0, f = 0;
    esp_err_t e = esp_vfs_fat_info(STORAGE_MOUNT, &t, &f);
    if (free_bytes) *free_bytes = f;
    if (total_bytes) *total_bytes = t;
    return e;
}
