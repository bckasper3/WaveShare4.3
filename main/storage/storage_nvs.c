/* NVS layer: every blob lives in two keys (slot A / B) wrapped by el_store_slot (generation +
 * CRC). The slot choice is envlogger_core's el_store_pair_*, host-tested with power cuts at every
 * byte. Called only with the storage lock held. */
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "storage_priv.h"
#include "el_util.h"

static const char *TAG = "stg_nvs";

#define NVS_NS        "envlog"
#define SLOT_BUF_MAX  (EL_SETTINGS_ENCODED_MAX + EL_STORE_SLOT_OVERHEAD)   /* largest blob */

typedef struct {
    const char *key[2];
    uint8_t     tag;
} blob_def_t;

/* NVS keys: stable, max 15 chars. Documented in docs/STORAGE.md. */
static const blob_def_t s_defs[STG_BLOB_COUNT] = {
    [STG_BLOB_SETTINGS]  = { { "set_a",   "set_b"   }, EL_STORE_TAG_SETTINGS },
    [STG_BLOB_EXPORT_WM] = { { "expwm_a", "expwm_b" }, EL_STORE_TAG_EXPORT_WM },
    [STG_BLOB_MIRROR_WM] = { { "mirwm_a", "mirwm_b" }, EL_STORE_TAG_MIRROR_WM },
    [STG_BLOB_UPLINK_WM] = { { "upwm_a",  "upwm_b"  }, EL_STORE_TAG_UPLINK_WM },
};

static nvs_handle_t    s_h;
static bool            s_open;
static el_store_pair_t s_pair[STG_BLOB_COUNT];
/* Static, not on the caller's stack (the main task has ~3.5 kB). Guarded by the storage lock. */
static uint8_t s_slot[2][SLOT_BUF_MAX];
static uint8_t s_wr[SLOT_BUF_MAX];

static esp_err_t open_ns(void)
{
    for (int i = 0; i < STG_BLOB_COUNT; i++) el_store_pair_init(&s_pair[i]);
    esp_err_t e = nvs_open(NVS_NS, NVS_READWRITE, &s_h);
    s_open = (e == ESP_OK);
    return e;
}

esp_err_t stg_nvs_open(void)
{
    /* NO_FREE_PAGES / NEW_VERSION_FOUND are NOT answered with an erase here: that would wipe
     * the settings silently. The device runs on defaults and reports it; an
     * engineer decides (storage_nvs_rebuild). */
    esp_err_t e = nvs_flash_init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s (NOT erased)", esp_err_to_name(e));
        return e;
    }
    e = open_ns();
    if (e != ESP_OK) ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(e));
    return e;
}

esp_err_t stg_nvs_erase_and_reopen(void)
{
    if (s_open) { nvs_close(s_h); s_open = false; }
    nvs_flash_deinit();
    esp_err_t e = nvs_flash_erase();
    if (e == ESP_OK) e = nvs_flash_init();
    if (e == ESP_OK) e = open_ns();
    return e;
}

/* Reads one key. *len = 0 when absent. A value larger than the buffer is reported as present
 * but unreadable (len = 1 of junk), so the pair logic counts it as damaged, not as "never". */
static esp_err_t read_key(const char *key, uint8_t *buf, size_t cap, size_t *len)
{
    size_t n = cap;
    esp_err_t e = nvs_get_blob(s_h, key, buf, &n);
    if (e == ESP_ERR_NVS_NOT_FOUND) { *len = 0; return ESP_OK; }
    if (e == ESP_ERR_NVS_INVALID_LENGTH) { buf[0] = 0; *len = 1; return ESP_OK; }
    if (e != ESP_OK) { *len = 0; return e; }
    *len = n;
    return ESP_OK;
}

el_err_t stg_nvs_load(stg_blob_t b, uint8_t *out, size_t cap, size_t *len,
                      el_store_accept_fn accept, void *ctx)
{
    if (len) *len = 0;
    if (!s_open || (unsigned)b >= STG_BLOB_COUNT) return EL_ERR_IO;
    size_t n[2];
    for (int i = 0; i < 2; i++) {
        esp_err_t e = read_key(s_defs[b].key[i], s_slot[i], sizeof s_slot[i], &n[i]);
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "read %s: %s", s_defs[b].key[i], esp_err_to_name(e));
            n[i] = 0;
        }
    }
    const uint8_t *pl = NULL;
    size_t pn = 0;
    el_err_t ke = el_store_pair_select(&s_pair[b], s_defs[b].tag, s_slot[0], n[0], s_slot[1], n[1],
                                       accept, ctx, &pl, &pn);
    if (ke != EL_OK) {
        if (ke == EL_ERR_CRC) ESP_LOGW(TAG, "%s/%s: no valid slot", s_defs[b].key[0], s_defs[b].key[1]);
        return ke;
    }
    if (pn > cap) return EL_ERR_SIZE;
    if (out && pn) memcpy(out, pl, pn);
    if (len) *len = pn;
    return EL_OK;
}

el_err_t stg_nvs_save(stg_blob_t b, const void *payload, size_t len)
{
    if (!s_open || (unsigned)b >= STG_BLOB_COUNT) return EL_ERR_IO;
    uint8_t slot;
    uint32_t gen;
    size_t n;
    el_store_pair_next(&s_pair[b], &slot, &gen);
    el_err_t ke = el_store_slot_encode(s_defs[b].tag, gen, payload, len, s_wr, sizeof s_wr, &n);
    if (ke != EL_OK) return ke;
    esp_err_t e = nvs_set_blob(s_h, s_defs[b].key[slot], s_wr, n);
    if (e == ESP_OK) e = nvs_commit(s_h);
    el_store_pair_written(&s_pair[b], slot, gen, e == ESP_OK);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "write %s: %s", s_defs[b].key[slot], esp_err_to_name(e));
        return EL_ERR_IO;
    }
    return EL_OK;
}

el_err_t stg_nvs_save_u32(stg_blob_t b, uint32_t v)
{
    uint8_t p[4];
    el_put_u32le(p, v);
    return stg_nvs_save(b, p, sizeof p);
}

el_err_t stg_nvs_load_u32(stg_blob_t b, uint32_t *v)
{
    uint8_t p[4];
    size_t n = 0;
    el_err_t e = stg_nvs_load(b, p, sizeof p, &n, NULL, NULL);
    if (e != EL_OK) return e;
    if (n != 4) return EL_ERR_FORMAT;
    *v = el_get_u32le(p);
    return EL_OK;
}

esp_err_t stg_nvs_usage(size_t *used, size_t *total)
{
    nvs_stats_t st;
    esp_err_t e = nvs_get_stats(NULL, &st);
    if (e != ESP_OK) return e;
    *used = st.used_entries;
    *total = st.total_entries;
    return ESP_OK;
}
