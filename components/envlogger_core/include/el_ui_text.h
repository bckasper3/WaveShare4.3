#pragma once
/* Text entry rules for the on-screen keyboard and numeric pad. Limits come from the field:
 * a tag field of EL_TAG_LEN bytes takes at most EL_TAG_LEN - 1 characters
 * (el_result.h: the string lengths are the on-flash limits). Pure C. */
#include <stdbool.h>
#include <stddef.h>
#include "el_err.h"

typedef enum {
    EL_UI_CS_TEXT = 0,    /* printable ASCII 0x20..0x7E (IDs, names) */
    EL_UI_CS_ID,          /* A-Z a-z 0-9 - _ . / # (no spaces: barcode/label style IDs) */
    EL_UI_CS_DIGITS,      /* 0-9 (PIN, integers >= 0) */
    EL_UI_CS_INT,         /* 0-9 and a leading '-' */
    EL_UI_CS_DECIMAL,     /* 0-9, one '.', a leading '-' */
    EL_UI_CS_COUNT
} el_ui_charset_t;

bool el_ui_text_char_ok(char c, el_ui_charset_t cs);

/* Append c to the NUL-terminated buf (cap bytes). max_chars caps the length further (0 = cap-1).
 * EL_ERR_ARG: NULL buf, cap 0, or c not allowed here (including a second '.', or '-' anywhere
 * but first). EL_ERR_SIZE: full. buf unchanged on error. */
el_err_t el_ui_text_append(char *buf, size_t cap, size_t max_chars, char c, el_ui_charset_t cs);
/* Remove the last character (no-op on ""). */
void     el_ui_text_backspace(char *buf);

