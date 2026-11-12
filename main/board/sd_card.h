#pragma once
/* Waveshare microSD slot: SPI (MOSI 11, SCK 12, MISO 13), CS on CH422G EXIO4 (driven through
 * board.h, not a GPIO: the SPI device is configured with CS = -1 and the board service holds
 * CS low for the duration of each locked transfer window). Implemented by the board service;
 * used ONLY by the storage service. Removable at any time: every call must cope with the card
 * vanishing (return an error, never crash or hang). */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Probe and mount FAT at mount_point. ESP_ERR_NOT_FOUND if no card. */
esp_err_t sd_card_mount(const char *mount_point);
void      sd_card_unmount(void);
bool      sd_card_mounted(void);
/* Re-probe (call every few seconds from storage): detects insert/remove. Returns true if the
 * mounted state changed. */
bool      sd_card_poll(void);
esp_err_t sd_card_space(uint64_t *free_bytes, uint64_t *total_bytes);
