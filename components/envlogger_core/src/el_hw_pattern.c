#include "el_hw_pattern.h"

uint16_t el_hw_pattern_rgb565(int x, int y, int w, int h)
{
    static const uint16_t bars[8] = {
        EL_RGB565(255, 255, 255), EL_RGB565(255, 255, 0), EL_RGB565(0, 255, 255), EL_RGB565(0, 255, 0),
        EL_RGB565(255, 0, 255),   EL_RGB565(255, 0, 0),   EL_RGB565(0, 0, 255),   EL_RGB565(0, 0, 0),
    };
    if (w <= 0 || h <= 0 || x < 0 || y < 0 || x >= w || y >= h) return 0;
    if (x == 0 || y == 0 || x == w - 1 || y == h - 1) return EL_RGB565(255, 0, 0);

    int bars_h = (h * 2) / 3;
    if (y < bars_h) {
        if (x % EL_HW_PATTERN_GRID_PX == 0 || y % EL_HW_PATTERN_GRID_PX == 0) return EL_RGB565(128, 128, 128);
        return bars[(x * 8) / w];
    }
    /* Ramps: 4 strips. Position -> 0..63 (6-bit) so green steps through all 64 codes and red/
     * blue (5-bit) through all 32. */
    int strip = ((y - bars_h) * 4) / (h - bars_h);
    unsigned v6 = (unsigned)((x * 64) / w);              /* 0..63 */
    unsigned r5 = v6 >> 1, g6 = v6, b5 = v6 >> 1;
    switch (strip) {
    case 0:  return (uint16_t)(r5 << 11);
    case 1:  return (uint16_t)(g6 << 5);
    case 2:  return (uint16_t)b5;
    default: return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
    }
}
