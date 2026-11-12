/* Waveshare microSD slot over SPI2 (MOSI 11, SCK 12, MISO 13). Chip select is CH422G EXIO4,
 * not a GPIO, so the SDSPI device runs with "no CS": the card is the only device on the bus,
 * CS is held low while a card is mounted, and every SD transaction runs under
 * board_expander_lock() so a DI read (which floats the IO port, releasing CS) can never land
 * inside one. Presence: no card-detect line, so el_hw_sd_poll decides when to probe. */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "board.h"
#include "board_priv.h"
#include "sd_card.h"
#include "el_hw_sdpoll.h"

#define SD_PIN_MOSI 11
#define SD_PIN_SCK  12
#define SD_PIN_MISO 13
#define SD_HOST     SPI2_HOST

static const char *TAG = "sd";
static SemaphoreHandle_t s_mtx;
static sdmmc_card_t *s_card;
static bool s_bus_ok;
static char s_mp[16] = "/sd";
static el_hw_sd_poll_t s_poll;

static uint32_t now_ms(void) { return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS); }

static esp_err_t locked_txn(int slot, sdmmc_command_t *cmd)
{
    board_expander_lock();
    esp_err_t e = sdspi_host_do_transaction(slot, cmd);
    board_expander_unlock();
    return e;
}

static void lazy_init(void)
{
    if (!s_mtx) {
        s_mtx = xSemaphoreCreateMutex();
        el_hw_sd_poll_init(&s_poll);
    }
}

static esp_err_t mount_locked(void)
{
    if (s_card) return ESP_OK;
    if (!s_bus_ok) {
        spi_bus_config_t bus = {
            .mosi_io_num = SD_PIN_MOSI, .miso_io_num = SD_PIN_MISO, .sclk_io_num = SD_PIN_SCK,
            .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 4096,
        };
        esp_err_t e = spi_bus_initialize(SD_HOST, &bus, SPI_DMA_CH_AUTO);
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
        s_bus_ok = true;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_HOST;
    host.do_transaction = locked_txn;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SD_HOST;
    slot.gpio_cs = SDSPI_SLOT_NO_CS;
    slot.gpio_cd = SDSPI_SLOT_NO_CD;
    slot.gpio_wp = SDSPI_SLOT_NO_WP;
    slot.gpio_int = SDSPI_SLOT_NO_INT;
    esp_vfs_fat_sdmmc_mount_config_t mc = {
        .format_if_mount_failed = false,      /* never format a customer's card */
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    board_sd_select(true);
    esp_err_t e = esp_vfs_fat_sdspi_mount(s_mp, &host, &slot, &mc, &s_card);
    if (e != ESP_OK) {
        s_card = NULL;
        board_sd_select(false);
        /* ESP_FAIL = card answered but no FAT volume; anything else = no card / no answer. */
        return e == ESP_FAIL ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "mounted %s (%s, %llu MB)", s_mp, s_card->cid.name,
             ((unsigned long long)s_card->csd.capacity * s_card->csd.sector_size) >> 20);
    return ESP_OK;
}

static void unmount_locked(void)
{
    if (!s_card) return;
    esp_vfs_fat_sdcard_unmount(s_mp, s_card);
    s_card = NULL;
    board_sd_select(false);
    ESP_LOGI(TAG, "unmounted");
}

esp_err_t sd_card_mount(const char *mount_point)
{
    lazy_init();
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (mount_point && !s_card) {
        strncpy(s_mp, mount_point, sizeof s_mp - 1);
        s_mp[sizeof s_mp - 1] = '\0';
    }
    esp_err_t e = mount_locked();
    el_hw_sd_poll_report(&s_poll, EL_HW_SD_ACT_MOUNT, e == ESP_OK, now_ms());
    xSemaphoreGive(s_mtx);
    return e;
}

void sd_card_unmount(void)
{
    lazy_init();
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    unmount_locked();
    el_hw_sd_poll_set_mounted(&s_poll, false, now_ms());
    xSemaphoreGive(s_mtx);
}

bool sd_card_mounted(void) { return s_card != NULL; }

bool sd_card_poll(void)
{
    lazy_init();
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    uint32_t now = now_ms();
    bool changed = false;
    el_hw_sd_action_t act = el_hw_sd_poll_next(&s_poll, now);
    if (act == EL_HW_SD_ACT_STATUS) {
        bool ok = s_card && sdmmc_get_status(s_card) == ESP_OK;   /* CMD13 */
        changed = el_hw_sd_poll_report(&s_poll, act, ok, now);
        if (changed) unmount_locked();
    } else if (act == EL_HW_SD_ACT_MOUNT) {
        changed = el_hw_sd_poll_report(&s_poll, act, mount_locked() == ESP_OK, now);
    }
    xSemaphoreGive(s_mtx);
    return changed;
}

esp_err_t sd_card_space(uint64_t *free_bytes, uint64_t *total_bytes)
{
    lazy_init();
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    uint64_t t = 0, f = 0;
    esp_err_t e = s_card ? esp_vfs_fat_info(s_mp, &t, &f) : ESP_ERR_NOT_FOUND;
    xSemaphoreGive(s_mtx);
    if (e == ESP_OK) {
        if (free_bytes) *free_bytes = f;
        if (total_bytes) *total_bytes = t;
    }
    return e;
}
