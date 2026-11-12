#pragma once
/* Board-service internals shared by board.c, display.c, sd_card.c and board_selftests.c.
 * Not for other services. */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* GT911 reset + address select (0x5D) through EXIO1 and GPIO4 (HARDWARE.md 5).
 * Also releases the LCD reset line. Takes ~180 ms. */
esp_err_t board_touch_reset(void);

/* SD chip select (EXIO4, active low). Held low while a card is mounted. */
esp_err_t board_sd_select(bool selected);

/* Self-test helpers: the IO shadow as it is now, and a raw output write + read-back in
 * output mode (restores the shadow afterwards). */
uint8_t   board_io_shadow(void);
esp_err_t board_io_readback(uint8_t pattern, uint8_t *raw);
bool      board_present(void);   /* CH422G answered at board_init() */
