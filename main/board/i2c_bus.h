#pragma once
/* One shared I2C master bus (GPIO8/9, 400 kHz) with a mutex. All devices go through here
 * (docs/HARDWARE.md 9). The GT911 touch driver uses the raw bus handle; the IDF master driver
 * serialises that against our transfers. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

esp_err_t i2c_bus_init(void);                       /* idempotent */
i2c_master_bus_handle_t i2c_bus_handle(void);       /* for drivers that need the raw bus (GT911) */

/* True if SDA and SCL both read high just before the bus was created (not stuck low). */
bool i2c_bus_lines_were_idle(void);

/* Add a 7-bit device; returns a handle used below. */
esp_err_t i2c_bus_add(uint8_t addr7, i2c_master_dev_handle_t *out);

esp_err_t i2c_bus_write(i2c_master_dev_handle_t dev, const uint8_t *buf, size_t len);
esp_err_t i2c_bus_write_read(i2c_master_dev_handle_t dev, const uint8_t *wbuf, size_t wlen,
                             uint8_t *rbuf, size_t rlen);
esp_err_t i2c_bus_read(i2c_master_dev_handle_t dev, uint8_t *rbuf, size_t rlen);

/* Helpers for 8-bit register maps with big-endian 16-bit values (big-endian register style). */
esp_err_t i2c_bus_write_reg16(i2c_master_dev_handle_t dev, uint8_t reg, uint16_t val);
esp_err_t i2c_bus_read_reg16(i2c_master_dev_handle_t dev, uint8_t reg, uint16_t *val);
esp_err_t i2c_bus_write_reg8(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val);
esp_err_t i2c_bus_read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t len);

/* ESP_OK if a device ACKs addr7. */
esp_err_t i2c_bus_probe(uint8_t addr7);

/* Engineer raw access (console I2C READ / WRITE): temporary device, 8-bit register. */
esp_err_t i2c_bus_raw_read(uint8_t addr7, uint8_t reg, uint8_t *buf, size_t len);
esp_err_t i2c_bus_raw_write(uint8_t addr7, uint8_t reg, const uint8_t *buf, size_t len);

/* Debug: scan and log every ACKing address. */
void i2c_bus_scan(void);
