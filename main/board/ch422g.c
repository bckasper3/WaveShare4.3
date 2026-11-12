#include "i2c_bus.h"
#include "ch422g.h"
#include "el_hw_ch422g.h"

static i2c_master_dev_handle_t s_mode, s_oc, s_io_wr, s_io_rd;

esp_err_t ch422g_init(void)
{
    if (s_mode) return ESP_OK;
    esp_err_t e = i2c_bus_add(EL_CH422G_CMD_MODE, &s_mode);
    if (e == ESP_OK) e = i2c_bus_add(EL_CH422G_CMD_OC, &s_oc);
    if (e == ESP_OK) e = i2c_bus_add(EL_CH422G_CMD_IO_WR, &s_io_wr);
    if (e == ESP_OK) e = i2c_bus_add(EL_CH422G_CMD_IO_RD, &s_io_rd);
    return e;
}

esp_err_t ch422g_write_mode(uint8_t mode)
{
    if (mode != EL_CH422G_MODE_INPUT && mode != EL_CH422G_MODE_OUTPUT) return ESP_ERR_INVALID_ARG;
    return i2c_bus_write(s_mode, &mode, 1);
}

esp_err_t ch422g_write_io(uint8_t io) { io &= (uint8_t)~EL_CH422G_IO_DI_MASK; return i2c_bus_write(s_io_wr, &io, 1); }
esp_err_t ch422g_write_oc(uint8_t oc) { oc &= EL_CH422G_OC_MASK; return i2c_bus_write(s_oc, &oc, 1); }
esp_err_t ch422g_read_io(uint8_t *raw) { return i2c_bus_read(s_io_rd, raw, 1); }
