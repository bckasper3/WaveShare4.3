/* USB service (docs/USB.md): CDC-ACM device only. */
#include "esp_log.h"
#include "esp_system.h"
#include "soc/rtc_cntl_reg.h"
#include "usb.h"
#include "usb_priv.h"

static const char *TAG = "usb";
static bool s_inited;

esp_err_t usb_init(void)
{
    if (s_inited) return ESP_OK;
    esp_err_t e = usb_cdc_install();
    if (e != ESP_OK) ESP_LOGE(TAG, "CDC install failed: %s", esp_err_to_name(e));
    s_inited = e == ESP_OK;
    return e;
}

void usb_reboot_to_bootloader(void)
{
    /* The ROM enters download mode on the next reset; esptool then talks over native USB
     * (USB-Serial-JTAG). */
    usb_cdc_uninstall();
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}

const el_test_def_t *usb_selftests(size_t *count)
{
    if (count) *count = 0;
    return NULL;
}
