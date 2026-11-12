#include <stdio.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "fw_update.h"
#include "app_config.h"

static const char *TAG = "fwupd";

bool fw_update_file_present(char *version_out, int len)
{
    /* TODO: fopen(STORAGE_SD_MOUNT "/" FW_UPDATE_FILE), el_store_fw_parse() on the first
     * EL_STORE_FW_PROBE_SIZE bytes, copy the version out. */
    (void)version_out; (void)len;
    return false;
}

void fw_update_from_sd_async(void)
{
    /* TODO: task: el_store_fw_check(), esp_ota_begin(esp_ota_get_next_update_partition(NULL)),
     * stream the file in 4 kB chunks with esp_ota_write, progress events, esp_ota_end,
     * esp_ota_set_boot_partition, restart. */
    ESP_LOGW(TAG, "firmware update from SD: not implemented yet");
}

esp_err_t fw_update_confirm_boot(void) { return esp_ota_mark_app_valid_cancel_rollback(); }
const char *fw_update_running_version(void) { return FW_VERSION_STR; }
