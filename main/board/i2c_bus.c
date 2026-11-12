#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "app_config.h"

static const char *TAG = "i2c";
static i2c_master_bus_handle_t s_bus;
static SemaphoreHandle_t s_mtx;
static bool s_idle_ok;
#define I2C_TIMEOUT_MS 50

esp_err_t i2c_bus_init(void)
{
    if (s_bus) return ESP_OK;
    if (!s_mtx) s_mtx = xSemaphoreCreateMutex();

    /* Stuck-bus check for the I2C_BUS self-test: both lines must idle high (external 4.7 k). */
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_I2C_SDA) | (1ULL << PIN_I2C_SCL),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&in);
    s_idle_ok = gpio_get_level(PIN_I2C_SDA) && gpio_get_level(PIN_I2C_SCL);
    if (!s_idle_ok) ESP_LOGE(TAG, "SDA/SCL not idle high: bus stuck?");

    i2c_master_bus_config_t cfg = {
        .i2c_port = 0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,   /* Waveshare has external pull-ups */
    };
    esp_err_t e = i2c_new_master_bus(&cfg, &s_bus);
    if (e != ESP_OK) { s_bus = NULL; ESP_LOGE(TAG, "bus init: %s", esp_err_to_name(e)); }
    return e;
}

i2c_master_bus_handle_t i2c_bus_handle(void) { return s_bus; }
bool i2c_bus_lines_were_idle(void) { return s_idle_ok; }

esp_err_t i2c_bus_add(uint8_t addr7, i2c_master_dev_handle_t *out)
{
    if (!s_bus) return ESP_ERR_INVALID_STATE;
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr7,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_bus, &dc, out);
}

#define LOCKED(expr) ({ esp_err_t _r; xSemaphoreTake(s_mtx, portMAX_DELAY); _r = (expr); xSemaphoreGive(s_mtx); _r; })

esp_err_t i2c_bus_write(i2c_master_dev_handle_t d, const uint8_t *b, size_t n)
{ return d ? LOCKED(i2c_master_transmit(d, b, n, I2C_TIMEOUT_MS)) : ESP_ERR_INVALID_STATE; }

esp_err_t i2c_bus_write_read(i2c_master_dev_handle_t d, const uint8_t *w, size_t wn, uint8_t *r, size_t rn)
{ return d ? LOCKED(i2c_master_transmit_receive(d, w, wn, r, rn, I2C_TIMEOUT_MS)) : ESP_ERR_INVALID_STATE; }

esp_err_t i2c_bus_read(i2c_master_dev_handle_t d, uint8_t *r, size_t rn)
{ return d ? LOCKED(i2c_master_receive(d, r, rn, I2C_TIMEOUT_MS)) : ESP_ERR_INVALID_STATE; }

esp_err_t i2c_bus_write_reg16(i2c_master_dev_handle_t d, uint8_t reg, uint16_t v)
{
    uint8_t b[3] = { reg, (uint8_t)(v >> 8), (uint8_t)v };
    return i2c_bus_write(d, b, 3);
}

esp_err_t i2c_bus_read_reg16(i2c_master_dev_handle_t d, uint8_t reg, uint16_t *v)
{
    uint8_t r[2];
    esp_err_t e = i2c_bus_write_read(d, &reg, 1, r, 2);
    if (e == ESP_OK) *v = ((uint16_t)r[0] << 8) | r[1];
    return e;
}

esp_err_t i2c_bus_write_reg8(i2c_master_dev_handle_t d, uint8_t reg, uint8_t v)
{
    uint8_t b[2] = { reg, v };
    return i2c_bus_write(d, b, 2);
}

esp_err_t i2c_bus_read_regs(i2c_master_dev_handle_t d, uint8_t reg, uint8_t *buf, size_t len)
{ return i2c_bus_write_read(d, &reg, 1, buf, len); }

esp_err_t i2c_bus_probe(uint8_t addr7)
{
    if (!s_bus) return ESP_ERR_INVALID_STATE;
    return LOCKED(i2c_master_probe(s_bus, addr7, I2C_TIMEOUT_MS));
}

esp_err_t i2c_bus_raw_read(uint8_t addr7, uint8_t reg, uint8_t *buf, size_t len)
{
    i2c_master_dev_handle_t d;
    esp_err_t e = i2c_bus_add(addr7, &d);
    if (e != ESP_OK) return e;
    e = i2c_bus_read_regs(d, reg, buf, len);
    i2c_master_bus_rm_device(d);
    return e;
}

esp_err_t i2c_bus_raw_write(uint8_t addr7, uint8_t reg, const uint8_t *buf, size_t len)
{
    uint8_t tmp[33];
    if (len > sizeof tmp - 1) return ESP_ERR_INVALID_SIZE;
    i2c_master_dev_handle_t d;
    esp_err_t e = i2c_bus_add(addr7, &d);
    if (e != ESP_OK) return e;
    tmp[0] = reg;
    for (size_t i = 0; i < len; i++) tmp[1 + i] = buf[i];
    e = i2c_bus_write(d, tmp, len + 1);
    i2c_master_bus_rm_device(d);
    return e;
}

void i2c_bus_scan(void)
{
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (i2c_bus_probe(a) == ESP_OK) ESP_LOGI(TAG, "found 0x%02X", a);
    }
}
