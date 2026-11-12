#pragma once
/* Error codes returned by every envlogger_core function. Core never logs, aborts or allocates;
 * callers translate these to esp_err_t / UI messages. EL_OK is always 0. */

typedef enum {
    EL_OK = 0,
    EL_ERR_ARG,          /* NULL pointer or argument out of the documented range */
    EL_ERR_SIZE,         /* caller's buffer too small; output is truncated / incomplete */
    EL_ERR_FORMAT,       /* bytes are not one of our objects (bad magic, erased flash) */
    EL_ERR_VERSION,      /* our object, but a version this firmware does not understand */
    EL_ERR_CRC,          /* our object, but corrupted (torn write, bit flip) */
    EL_ERR_RANGE,        /* value decoded but outside physical limits */
    EL_ERR_DENIED,       /* caller's access level may not do this (serial: ERR 4) */
    EL_ERR_IO,           /* storage / bus I/O failed (flash, FAT, I2C); used by I/O backends */
} el_err_t;

const char *el_err_name(el_err_t e);
