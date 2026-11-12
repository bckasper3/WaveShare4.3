#pragma once
/* CH422G IO expander: raw byte transport only (address-as-command, one data byte each).
 * Board-private: only board.c / board_selftests.c call this. Polarity, the DI bits and the
 * shadow registers are el_hw_ch422g (envlogger_core); callers hold board_expander_lock().
 * Espressif's driver is not used because it skips 0-byte writes (docs/HARDWARE.md 2.1). */
#include <stdint.h>
#include "esp_err.h"

esp_err_t ch422g_init(void);                 /* registers the four command addresses */
esp_err_t ch422g_write_mode(uint8_t mode);   /* EL_CH422G_MODE_INPUT / _OUTPUT only */
esp_err_t ch422g_write_io(uint8_t io);       /* caller passes el_hw_ch422g_io_byte() */
esp_err_t ch422g_write_oc(uint8_t oc);       /* caller passes el_hw_ch422g_oc_byte() */
esp_err_t ch422g_read_io(uint8_t *raw);
