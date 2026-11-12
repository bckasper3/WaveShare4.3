#pragma once
/* Text formatting for the touchscreen UI (and anything else that shows numbers to people).
 * Pure C, no clock, no locale: every function takes the time / settings it needs.
 * Every formatter writes a NUL-terminated string and returns EL_ERR_SIZE when cap is too small
 * (the output is then truncated but still terminated), EL_ERR_ARG for a NULL buffer / cap 0.
 * Values that cannot be shown (NAN, infinity, "not applicable") become EL_UI_NA. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_err.h"
#include "el_settings.h"

#define EL_UI_NA            "---"

/* ---- numbers ---- */
/* "24.6 °C" (UTF-8 degree sign allowed in unit). unit may be NULL/"" (no space added).
 * Non-finite -> "---". decimals 0..3 (clamped). */
el_err_t el_ui_fmt_fixed(float value, uint8_t decimals, const char *unit, char *buf, size_t cap);

/* ---- durations ---- */
/* Seconds as "45 s", "5 min", "5 min 20 s", "2 h", "1 h 30 min", "1 d", "1 d 2 h"
 * (logging interval, time to the next automatic reading). */
el_err_t el_ui_fmt_secs(uint32_t seconds, char *buf, size_t cap);
/* Seconds as "04:12:09" or "3 d 04:12:09". */
el_err_t el_ui_fmt_uptime(uint32_t seconds, char *buf, size_t cap);
/* Relative age: "just now" (< 60 s), "5 min ago", "3 h ago", "2 d ago" (< 60 d), else
 * "> 60 d ago". then > now (clock set back) -> "just now"; then <= 0 (unknown) -> "---". */
el_err_t el_ui_fmt_ago(int64_t now_s, int64_t then_s, char *buf, size_t cap);

/* ---- local date / time (utc_offset_min = the utc_offset_min setting, -720..840) ---- */
/* ISO "2026-10-30", US "10/30/2026", EU "30.10.2026". Unknown fmt -> ISO. */
el_err_t el_ui_fmt_date(int64_t unix_s, int16_t utc_offset_min, uint8_t date_fmt, char *buf, size_t cap);
/* "14:05" or "14:05:09" (24 h, as used in labs). */
el_err_t el_ui_fmt_time(int64_t unix_s, int16_t utc_offset_min, bool seconds, char *buf, size_t cap);
/* date + ' ' + time without seconds. */
el_err_t el_ui_fmt_datetime(int64_t unix_s, int16_t utc_offset_min, uint8_t date_fmt, char *buf, size_t cap);
/* Local short form for lists: "Oct 30 14:05" (needs cap >= 13). */
el_err_t el_ui_fmt_short(int64_t unix_s, int16_t utc_offset_min, char *buf, size_t cap);

/* ---- paging (History) ---- */
/* Pages of page_size rows over count items (at least 1 page, even when empty). */
uint32_t el_ui_page_count(uint32_t count, uint32_t page_size);
/* Newest-first paging: index (0 = oldest) of the first row on page `page`, and how many rows the
 * page has. Returns false (and *n = 0) when the page does not exist. */
bool     el_ui_page_newest_first(uint32_t count, uint32_t page_size, uint32_t page,
                                 uint32_t *start_index, uint32_t *n);
