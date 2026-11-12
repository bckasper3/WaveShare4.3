/* LVGL v8.3 configuration for the PC simulator. Mirrors the device where it matters:
 * RGB565, custom (malloc) memory as with CONFIG_LV_MEM_CUSTOM=y, 800x480. Everything not set
 * here takes the lv_conf_internal.h default (all widgets enabled, as the ESP-IDF Kconfig does).
 * The tick comes from the simulator's clock so headless screenshots are deterministic. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH          16
#define LV_COLOR_16_SWAP        0

#define LV_MEM_CUSTOM           1
#define LV_MEM_CUSTOM_INCLUDE   <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC     malloc
#define LV_MEM_CUSTOM_FREE      free
#define LV_MEM_CUSTOM_REALLOC   realloc

#define LV_DISP_DEF_REFR_PERIOD 16
#define LV_INDEV_DEF_READ_PERIOD 16

#define LV_TICK_CUSTOM          1
#define LV_TICK_CUSTOM_INCLUDE  "sim_tick.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (sim_tick_ms())

#define LV_DPI_DEF              130
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1

#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

#define LV_USE_PERF_MONITOR     0
#define LV_USE_MEM_MONITOR      0
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   0
#define LV_USE_USER_DATA        1

#endif
