#pragma once
/* 800x480 RGB panel (ST7262) + GT911 touch + LVGL 8 via esp_lvgl_port.
 * Ported from Waveshare ESP-IDF demo 08_lvgl_Porting (HARDWARE.md 4-5): two frame
 * buffers in PSRAM used directly by LVGL (direct mode, no tearing), 10-line bounce buffer,
 * 16 MHz PCLK. The backlight is the CH422G DISP line: on/off only. */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t display_init(void);          /* panel, touch, LVGL task; backlight on at the end */
bool display_ready(void);              /* panel and LVGL running */
bool display_touch_ok(void);           /* GT911 driver created */
bool display_lock(uint32_t timeout_ms); /* wrap every LVGL call made outside the LVGL task */
void display_unlock(void);
/* On/off only: the Waveshare DISP line has no PWM (TODO: dimming needs a hardware change). */
esp_err_t display_set_brightness(uint8_t pct);
void display_sleep(bool sleep);        /* idle timeout: backlight off, touch still wakes */

/* Self-tests (board_selftests.c). */
esp_err_t display_show_test_pattern(uint32_t hold_ms);   /* el_hw_pattern, then LVGL redraws */
esp_err_t display_touch_read_id(uint8_t id[4]);          /* GT911 0x8140..0x8143 */
