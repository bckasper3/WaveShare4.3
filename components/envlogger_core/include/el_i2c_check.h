#pragma once
/* Compare an I2C bus scan against the expected device table (test I2C_BUS).
 * Missing required device, or more than EL_I2C_UNEXPECTED_MAX unknown ACKs (bus fault) -> FAIL;
 * missing optional device or an unexpected address -> WARN.
 * Unexpected addresses are listed: on first sensor bring-up this is how the sensor's address
 * shows up. A device may answer on a range of addresses (CH422G); one ACK in the range counts. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_err.h"
#include "el_selftest.h"

typedef struct { uint8_t bits[16]; } el_i2c_scan_t;   /* bit a = 7-bit address a ACKed */

void el_i2c_scan_clear(el_i2c_scan_t *s);
void el_i2c_scan_mark(el_i2c_scan_t *s, uint8_t addr7);
bool el_i2c_scan_has(const el_i2c_scan_t *s, uint8_t addr7);

typedef struct {
    uint8_t     first, last;   /* inclusive address range; first == last for a single address */
    const char *name;          /* "PCF85063" */
    bool        required;
} el_i2c_expect_t;

#define EL_I2C_EXPECT_MAX      32
#define EL_I2C_UNEXPECTED_MAX  16

typedef struct {
    el_test_outcome_t outcome;
    uint32_t missing;                         /* bit i = expect[i] not found */
    uint8_t  n_found;                         /* expected entries found */
    uint8_t  n_unexpected;                    /* total unexpected addresses */
    uint8_t  unexpected[EL_I2C_UNEXPECTED_MAX];
} el_i2c_check_t;

el_err_t el_i2c_check(const el_i2c_scan_t *scan, const el_i2c_expect_t *expect, size_t n, el_i2c_check_t *out);

/* "missing PCF85063(0x51); unexpected 0x2A" or "4 devices OK". Truncates safely. */
void el_i2c_check_describe(const el_i2c_check_t *c, const el_i2c_expect_t *expect, size_t n, char *dst, size_t cap);
