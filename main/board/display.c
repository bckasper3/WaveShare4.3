#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"
#include "board.h"
#include "board_priv.h"
#include "display.h"
#include "i2c_bus.h"
#include "app_config.h"
#include "el_hw_pattern.h"

static const char *TAG = "display";
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_tp_io;
static esp_lcd_touch_handle_t s_tp;
static lv_disp_t *s_disp;
static bool s_ready;

#define BOUNCE_LINES 10

static esp_err_t panel_init(void)
{
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = LCD_PCLK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_pulse_width = 4, .hsync_back_porch = 8, .hsync_front_porch = 8,
            .vsync_pulse_width = 4, .vsync_back_porch = 8, .vsync_front_porch = 8,
            .flags.pclk_active_neg = 1,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 2,                                   /* LVGL direct mode, tear-free */
        .bounce_buffer_size_px = LCD_H_RES * BOUNCE_LINES,
        .dma_burst_size = 64,
        .hsync_gpio_num = PIN_LCD_HSYNC,
        .vsync_gpio_num = PIN_LCD_VSYNC,
        .de_gpio_num = PIN_LCD_DE,
        .pclk_gpio_num = PIN_LCD_PCLK,
        .disp_gpio_num = -1,                            /* DISP is CH422G EXIO2 */
        .data_gpio_nums = LCD_DATA_PINS,
        .flags.fb_in_psram = 1,
    };
    esp_err_t e = esp_lcd_new_rgb_panel(&cfg, &s_panel);
    if (e == ESP_OK) e = esp_lcd_panel_init(s_panel);
    return e;
}

static esp_err_t touch_init(void)
{
    esp_err_t e = board_touch_reset();
    if (e != ESP_OK) return e;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.scl_speed_hz = I2C_FREQ_HZ;
    e = esp_lcd_new_panel_io_i2c(i2c_bus_handle(), &io_cfg, &s_tp_io);
    if (e != ESP_OK) return e;
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = -1,          /* reset done above through the CH422G */
        .int_gpio_num = -1,          /* polled by the LVGL task, like the Waveshare demo */
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    return esp_lcd_touch_new_i2c_gt911(s_tp_io, &tp_cfg, &s_tp);
}

esp_err_t display_init(void)
{
    if (s_ready) return ESP_OK;
    esp_err_t e = panel_init();
    if (e != ESP_OK) { ESP_LOGE(TAG, "RGB panel: %s", esp_err_to_name(e)); return e; }

    e = touch_init();
    if (e != ESP_OK) { ESP_LOGE(TAG, "GT911: %s (continuing without touch)", esp_err_to_name(e)); s_tp = NULL; }

    lvgl_port_cfg_t lp = ESP_LVGL_PORT_INIT_CONFIG();
    lp.task_priority = 4;           /* FIRMWARE_ARCH.md section 2 */
    lp.task_stack = 8192;
    lp.task_affinity = 1;
    e = lvgl_port_init(&lp);
    if (e != ESP_OK) { ESP_LOGE(TAG, "lvgl_port_init: %s", esp_err_to_name(e)); return e; }

    lvgl_port_display_cfg_t dc = {
        .panel_handle = s_panel,
        .buffer_size = LCD_H_RES * LCD_V_RES,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .flags = { .buff_spiram = 1, .direct_mode = 1 },
    };
    lvgl_port_display_rgb_cfg_t rc = { .flags = { .bb_mode = 1, .avoid_tearing = 1 } };
    s_disp = lvgl_port_add_disp_rgb(&dc, &rc);
    if (!s_disp) { ESP_LOGE(TAG, "lvgl_port_add_disp_rgb failed"); return ESP_FAIL; }

    if (s_tp) {
        lvgl_port_touch_cfg_t tc = { .disp = s_disp, .handle = s_tp };
        if (!lvgl_port_add_touch(&tc)) ESP_LOGE(TAG, "lvgl_port_add_touch failed");
    }
    s_ready = true;
    board_backlight(true);
    ESP_LOGI(TAG, "display up (touch %s)", s_tp ? "ok" : "missing");
    return ESP_OK;
}

bool display_ready(void)    { return s_ready; }
bool display_touch_ok(void) { return s_tp != NULL; }

bool display_lock(uint32_t timeout_ms)
{
    if (!s_ready) return false;
    return lvgl_port_lock(timeout_ms);
}

void display_unlock(void) { if (s_ready) lvgl_port_unlock(); }

esp_err_t display_set_brightness(uint8_t pct) { return board_backlight(pct > 0); }

void display_sleep(bool sleep) { board_backlight(!sleep); }

esp_err_t display_show_test_pattern(uint32_t hold_ms)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    const int lines = 16;
    uint16_t *buf = heap_caps_malloc(LCD_H_RES * lines * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!buf) return ESP_ERR_NO_MEM;
    /* Hold the LVGL lock so LVGL cannot flush over the pattern while it is shown. */
    if (!lvgl_port_lock(1000)) { heap_caps_free(buf); return ESP_ERR_TIMEOUT; }
    esp_err_t e = ESP_OK;
    for (int y0 = 0; y0 < LCD_V_RES && e == ESP_OK; y0 += lines) {
        int n = (LCD_V_RES - y0) < lines ? (LCD_V_RES - y0) : lines;
        for (int y = 0; y < n; y++)
            for (int x = 0; x < LCD_H_RES; x++)
                buf[y * LCD_H_RES + x] = el_hw_pattern_rgb565(x, y0 + y, LCD_H_RES, LCD_V_RES);
        e = esp_lcd_panel_draw_bitmap(s_panel, 0, y0, LCD_H_RES, y0 + n, buf);
    }
    vTaskDelay(pdMS_TO_TICKS(hold_ms));
    lv_obj_invalidate(lv_scr_act());
    lvgl_port_unlock();
    heap_caps_free(buf);
    return e;
}

esp_err_t display_touch_read_id(uint8_t id[4])
{
    if (!s_tp_io) return ESP_ERR_INVALID_STATE;
    return esp_lcd_panel_io_rx_param(s_tp_io, 0x8140, id, 4);
}
