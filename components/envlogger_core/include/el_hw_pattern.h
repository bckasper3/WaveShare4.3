#pragma once
/* Display self-test pattern (test DISPLAY, serial DISP TEST). Computed per pixel so the ESP32
 * side can stream it straight into a frame buffer without LVGL.
 *
 * Layout for a w x h panel (800 x 480 here), RGB565:
 *   top 2/3:    8 vertical colour bars, left to right white, yellow, cyan, green, magenta,
 *               red, blue, black, overlaid with a 1-px mid-grey grid every 40 px;
 *   bottom 1/3: four horizontal ramps (red, green, blue, grey), dark at the left to full
 *               at the right, stepping through every code of each channel. A stuck, swapped
 *               or missing RGB data line shows up as a broken or non-monotonic ramp.
 *   frame:      the outermost 1-px border is red on every edge (checks porch/timing: a
 *               missing or doubled border line = wrong h/v porch or resolution).
 * Pure function; thread-safe. */
#include <stdint.h>

#define EL_HW_PATTERN_GRID_PX 40

#define EL_RGB565(r8, g8, b8) ((uint16_t)((((r8) & 0xF8u) << 8) | (((g8) & 0xFCu) << 3) | ((b8) >> 3)))

/* Colour of pixel (x, y). Out-of-range coordinates return black. */
uint16_t el_hw_pattern_rgb565(int x, int y, int w, int h);
