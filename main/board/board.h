#pragma once
/* Board service: Waveshare ESP32-S3-Touch-LCD-4.3B hardware access (docs/HARDWARE.md).
 * Thread-safe. Lower-level drivers sit beside it: i2c_bus.h, ch422g.h, display.h, rtc.h,
 * sd_card.h.
 *
 * CH422G sharing rules (HARDWARE.md section 3):
 *   - Output polarity is handled here: callers say on/off, never raw bits.
 *   - Reading DI0/DI1 switches the whole IO port to input for ~2-5 ms. During that window the
 *     backlight is pulled ON and SD chip-select (EXIO4) is released, so a DI read and an SD
 *     transfer must never overlap: both take board_expander_lock(). */
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "el_selftest.h"

/* Bring up I2C and the CH422G with safe defaults, FIRST thing at boot:
 * DO0 OFF, DO1 OFF, backlight off, LCD/touch held in reset, SD CS high. */
esp_err_t board_init(void);

esp_err_t board_set_do1(bool on);                  /* alarm output DO1 (board terminal) */
esp_err_t board_backlight(bool on);
esp_err_t board_read_di(bool *di0_active, bool *di1_active);   /* polarity corrected */

typedef struct {
    bool do0_on, do1_on;          /* output transistor sinking */
    bool di0, di1;                /* active, polarity corrected (from the last read) */
    bool backlight, lcd_rst, tp_rst, sd_cs;
} board_io_t;
void board_get_io(board_io_t *out);                /* cached state, no bus traffic */

/* Engineer raw access (IO SET): name in {DO0, DO1, BL, LCD_RST, TP_RST}. ESP_ERR_INVALID_ARG
 * for anything else (DI0/DI1/EXIO5 can never be driven). */
esp_err_t board_io_set_raw(const char *name, bool level);

/* Serialises CH422G port-mode switches against SD card transfers. */
void board_expander_lock(void);
void board_expander_unlock(void);

/* Self-tests for everything on this board layer: I2C_BUS, CH422G, RTC, TOUCH, PSRAM,
 * FLASH_ID, DISPLAY (interactive), TOUCH_CORNERS (interactive), DIO_LOOP (fixture), SD
 * (optional: WARN if no card). IDs are stable (docs/DIAGNOSTICS.md section 7). */
const el_test_def_t *board_selftests(size_t *count);
