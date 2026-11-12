#pragma once
/* CSV result format (docs/PROTOCOLS.md section 2). One formatter for the serial stream,
 * LOG? replies and the SD-card export / mirror, so all of them always match.
 * Text fields are quoted per RFC 4180 when needed. Text that a spreadsheet would run as a
 * formula (starts with = + - @) is prefixed with ' (CSV-injection guard). */
#include <stddef.h>
#include "el_err.h"
#include "el_result.h"

/* Device-level columns that are not part of the result itself. */
typedef struct {
    const char *device_serial;
    const char *fw_version;
    unsigned    decimals;        /* digits after the point for temperature / humidity, 0..3 */
} el_csv_ctx_t;

/* Header row without line ending. */
const char *el_csv_header(void);

/* One escaped text field into dst (no separator). EL_ERR_SIZE if it does not fit. */
el_err_t el_csv_escape(char *dst, size_t cap, const char *src);

/* One result row without line ending. EL_ERR_SIZE if cap is too small (dst then holds a
 * truncated row - never send it). EL_CSV_ROW_MAX bytes is always enough. */
el_err_t el_csv_format_result(char *dst, size_t cap, const el_result_t *r, const el_csv_ctx_t *ctx);

#define EL_CSV_ROW_MAX 512
