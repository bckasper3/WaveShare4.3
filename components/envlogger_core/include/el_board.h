#pragma once
/* Board description data for the Waveshare ESP32-S3-Touch-LCD-4.3B + external SHT sensor.
 * Data only (no driver code) so it is unit-tested on the PC against docs/HARDWARE.md.
 * A future board revision adds a second table and selects it at build time. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_i2c_check.h"

extern const el_i2c_expect_t EL_BOARD_I2C_EXPECT[];
extern const size_t          EL_BOARD_I2C_EXPECT_N;

/* True if addr7 is a legal 7-bit address not used by any listed device. Reserved addresses
 * 0x00-0x07 and 0x78-0x7F are never free. */
bool el_board_i2c_addr_free(uint8_t addr7);
