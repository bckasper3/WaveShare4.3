#pragma once
/* Sensirion SHT4x (SHT40/41/45) and SHT3x (SHT30/31/35) temperature + humidity driver behind
 * the el_sensor interface. Pure C: the firmware supplies the I2C transfers (el_sht_io_t), the
 * driver never blocks and never reads a clock.
 *
 * Protocol (Sensirion datasheets):
 *   SHT4x: write 0xFD (measure T + RH, high repeatability), wait >= 8.3 ms, read 6 bytes.
 *   SHT3x: write 0x24 0x00 (single shot, high repeatability, no clock stretching),
 *          wait >= 15 ms, read 6 bytes.
 *   6 bytes = T msb, T lsb, CRC, RH msb, RH lsb, CRC. CRC-8: polynomial 0x31, init 0xFF,
 *   no reflection, no final XOR (check: 0xBE 0xEF -> 0x92).
 *   T  = -45 + 175 * raw / 65535 (both families)
 *   RH = -6 + 125 * raw / 65535 (SHT4x)  |  100 * raw / 65535 (SHT3x); clamped to 0..100.
 * While a conversion runs the sensor NACKs its address; the driver re-reads until
 * EL_SHT_READ_TIMEOUT_MS has passed, then reports TIMEOUT. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_err.h"
#include "el_sensor.h"

#define EL_SHT_ADDR_DEFAULT      0x44
#define EL_SHT_ADDR_ALT          0x45     /* SHT3x ADDR pin high, SHT4x "-B" parts */
#define EL_SHT4X_WAIT_MS         10u
#define EL_SHT3X_WAIT_MS         16u
#define EL_SHT_READ_TIMEOUT_MS   100u

typedef enum { EL_SHT4X = 0, EL_SHT3X, EL_SHT_MODEL_COUNT } el_sht_model_t;

/* I2C transfers supplied by the firmware. Return 0 when the device ACKed and the transfer
 * completed, anything else for a NACK / bus error. */
typedef struct {
    int  (*write)(void *bus, uint8_t addr7, const uint8_t *data, size_t len);
    int  (*read)(void *bus, uint8_t addr7, uint8_t *data, size_t len);
    void  *bus;
} el_sht_io_t;

/* Driver state. Treat as opaque. */
typedef struct {
    el_sht_model_t model;
    uint8_t        addr;
    el_sht_io_t    io;
    bool           busy;
    uint32_t       start_ms;
} el_sht_t;

/* EL_ERR_ARG: NULL, model unknown, address not 0x44/0x45, or io without write/read. */
el_err_t    el_sht_init(el_sht_t *s, el_sht_model_t model, uint8_t addr7, const el_sht_io_t *io);
el_sensor_t el_sht_driver(el_sht_t *s);
const char *el_sht_model_name(el_sht_model_t m);     /* "SHT4x", "SHT3x" */

/* ---- protocol helpers (also used by the tests) ---- */
uint8_t  el_sht_crc8(const uint8_t *data, size_t len);
float    el_sht_temp_c(uint16_t raw);
float    el_sht_rh_pct(el_sht_model_t m, uint16_t raw);
/* Decode a 6-byte reply. EL_SENSOR_ERR_CRC if either CRC is wrong (outputs untouched). */
el_sensor_err_t el_sht_decode(el_sht_model_t m, const uint8_t b[6], float *temp_c, float *rh_pct);
/* Measurement command bytes for the model; *len = 1 (SHT4x) or 2 (SHT3x). */
const uint8_t *el_sht_meas_cmd(el_sht_model_t m, size_t *len);
/* Soft-reset command bytes (SHT4x 0x94, SHT3x 0x30 0xA2). */
const uint8_t *el_sht_reset_cmd(el_sht_model_t m, size_t *len);
