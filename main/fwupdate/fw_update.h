#pragma once
/* Firmware update from the SD card (skeleton): looks for /sd/FIRMWARE.BIN, checks project name,
 * version (no silent downgrade) and the image hash with el_store_fw_*, writes the inactive OTA
 * slot, reboots. On first boot of a new image: self-test, then
 * esp_ota_mark_app_valid_cancel_rollback(). No update over Wi-Fi. */
#include <stdbool.h>
#include "esp_err.h"

#define FW_UPDATE_FILE  "FIRMWARE.BIN"

bool      fw_update_file_present(char *version_out, int len);
void      fw_update_from_sd_async(void);
esp_err_t fw_update_confirm_boot(void);    /* call after successful self-test at startup */
const char *fw_update_running_version(void);
