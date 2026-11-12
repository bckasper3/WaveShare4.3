/* Board service: CH422G outputs/inputs with the rules of HARDWARE.md 2-3.
 * All expander traffic happens under one recursive mutex (board_expander_lock), which also
 * serialises SD transfers (sd_card.c) against DI reads: a DI read switches the IO port to
 * input, which releases SD chip-select and pulls the backlight on for ~2-5 ms. */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "board.h"
#include "board_priv.h"
#include "ch422g.h"
#include "i2c_bus.h"
#include "app_config.h"
#include "el_hw_ch422g.h"

static const char *TAG = "board";
static SemaphoreHandle_t s_lock;
static el_hw_ch422g_t s_sh;
static bool s_di0, s_di1;
static bool s_present;

void board_expander_lock(void)   { xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
void board_expander_unlock(void) { xSemaphoreGiveRecursive(s_lock); }

static esp_err_t write_oc(void) { return ch422g_write_oc(el_hw_ch422g_oc_byte(&s_sh)); }
static esp_err_t write_io(void) { return ch422g_write_io(el_hw_ch422g_io_byte(&s_sh)); }

esp_err_t board_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutex();
    esp_err_t e = i2c_bus_init();
    if (e == ESP_OK) e = ch422g_init();
    if (e != ESP_OK) return e;

    board_expander_lock();
    el_hw_ch422g_boot(&s_sh);
    /* Outputs OFF is the very first write. Retry: a slave that is still in power-on reset can
     * NACK the first byte. */
    for (int i = 0; i < 3; i++) {
        e = write_oc();
        if (e == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (e == ESP_OK) e = write_io();                          /* latch before driving */
    if (e == ESP_OK) e = ch422g_write_mode(EL_CH422G_MODE_OUTPUT);
    board_expander_unlock();

    s_present = (e == ESP_OK);
    if (!s_present) ESP_LOGE(TAG, "CH422G not responding: %s", esp_err_to_name(e));
    return e;
}

bool board_present(void) { return s_present; }

static esp_err_t set_oc(uint8_t bit, bool on)
{
    board_expander_lock();
    esp_err_t e = (el_hw_ch422g_oc_set(&s_sh, bit, on) == EL_OK) ? write_oc() : ESP_ERR_INVALID_ARG;
    board_expander_unlock();
    return e;
}

static esp_err_t set_io(uint8_t bit, bool level)
{
    board_expander_lock();
    esp_err_t e = (el_hw_ch422g_io_set(&s_sh, bit, level) == EL_OK) ? write_io() : ESP_ERR_INVALID_ARG;
    board_expander_unlock();
    return e;
}

esp_err_t board_set_do1(bool on)               { return set_oc(EL_OC_DO1, on); }
esp_err_t board_backlight(bool on)             { return set_io(EL_EXIO_DISP, on); }
esp_err_t board_sd_select(bool selected)       { return set_io(EL_EXIO_SD_CS, !selected); }

esp_err_t board_read_di(bool *di0_active, bool *di1_active)
{
    uint8_t raw = 0xFF;
    board_expander_lock();
    esp_err_t e = ch422g_write_mode(EL_CH422G_MODE_INPUT);
    if (e == ESP_OK) {
        /* +1 tick so the wait is never shorter than the settle time. */
        vTaskDelay(pdMS_TO_TICKS(EL_CH422G_INPUT_SETTLE_MS) + 1);
        e = ch422g_read_io(&raw);
    }
    /* Always go back to outputs, even after an error: SD CS and the resets depend on it. */
    esp_err_t e2 = ch422g_write_mode(EL_CH422G_MODE_OUTPUT);
    if (e2 == ESP_OK) e2 = write_io();
    if (e == ESP_OK) el_hw_ch422g_di_decode(raw, &s_di0, &s_di1);
    bool d0 = s_di0, d1 = s_di1;
    board_expander_unlock();
    if (e == ESP_OK) e = e2;
    if (e == ESP_OK) {
        if (di0_active) *di0_active = d0;
        if (di1_active) *di1_active = d1;
    }
    return e;
}

void board_get_io(board_io_t *out)
{
    if (!out) return;
    board_expander_lock();
    out->do0_on    = el_hw_ch422g_oc_is_on(&s_sh, EL_OC_DO0);
    out->do1_on    = el_hw_ch422g_oc_is_on(&s_sh, EL_OC_DO1);
    out->di0       = s_di0;
    out->di1       = s_di1;
    out->backlight = el_hw_ch422g_io_get(&s_sh, EL_EXIO_DISP);
    out->lcd_rst   = el_hw_ch422g_io_get(&s_sh, EL_EXIO_LCD_RST);
    out->tp_rst    = el_hw_ch422g_io_get(&s_sh, EL_EXIO_TP_RST);
    out->sd_cs     = el_hw_ch422g_io_get(&s_sh, EL_EXIO_SD_CS);
    board_expander_unlock();
}

esp_err_t board_io_set_raw(const char *name, bool level)
{
    el_hw_io_kind_t kind;
    uint8_t bit;
    if (el_hw_ch422g_parse_name(name, &kind, &bit) != EL_OK) return ESP_ERR_INVALID_ARG;
    return kind == EL_HW_IO_OC ? set_oc(bit, level) : set_io(bit, level);
}

esp_err_t board_touch_reset(void)
{
    gpio_config_t out = { .pin_bit_mask = 1ULL << PIN_TOUCH_INT, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&out);

    board_expander_lock();
    el_hw_ch422g_io_set(&s_sh, EL_EXIO_LCD_RST, true);
    el_hw_ch422g_io_set(&s_sh, EL_EXIO_TP_RST, false);
    esp_err_t e = write_io();
    board_expander_unlock();
    vTaskDelay(pdMS_TO_TICKS(10));               /* reset low > 100 us */

    gpio_set_level(PIN_TOUCH_INT, 0);           /* INT low at release -> address 0x5D */
    vTaskDelay(pdMS_TO_TICKS(2));               /* INT set up > 100 us before release */

    if (e == ESP_OK) e = set_io(EL_EXIO_TP_RST, true);
    vTaskDelay(pdMS_TO_TICKS(10));              /* > 5 ms with INT held */
    vTaskDelay(pdMS_TO_TICKS(60));              /* then > 50 ms */

    gpio_config_t in = { .pin_bit_mask = 1ULL << PIN_TOUCH_INT, .mode = GPIO_MODE_INPUT };
    gpio_config(&in);                           /* GT911 drives INT from now on */
    vTaskDelay(pdMS_TO_TICKS(100));             /* firmware start-up before the first I2C read */
    return e;
}

uint8_t board_io_shadow(void)
{
    board_expander_lock();
    uint8_t v = el_hw_ch422g_io_byte(&s_sh);
    board_expander_unlock();
    return v;
}


esp_err_t board_io_readback(uint8_t pattern, uint8_t *raw)
{
    board_expander_lock();
    esp_err_t e = ch422g_write_io(pattern);
    if (e == ESP_OK) e = ch422g_read_io(raw);    /* output mode: reads the output latches */
    esp_err_t e2 = write_io();
    board_expander_unlock();
    return e == ESP_OK ? e2 : e;
}
