#include <string.h>
#include "el_ui_text.h"

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool el_ui_text_char_ok(char c, el_ui_charset_t cs)
{
    switch (cs) {
    case EL_UI_CS_TEXT:    return c >= 0x20 && c <= 0x7E;
    case EL_UI_CS_ID:
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || is_digit(c) ||
               c == '-' || c == '_' || c == '.' || c == '/' || c == '#';
    case EL_UI_CS_DIGITS:  return is_digit(c);
    case EL_UI_CS_INT:     return is_digit(c) || c == '-';
    case EL_UI_CS_DECIMAL: return is_digit(c) || c == '-' || c == '.';
    default:               return false;
    }
}

el_err_t el_ui_text_append(char *buf, size_t cap, size_t max_chars, char c, el_ui_charset_t cs)
{
    if (!buf || !cap || !el_ui_text_char_ok(c, cs)) return EL_ERR_ARG;
    size_t len = strlen(buf);
    /* Position rules for signed / decimal numbers. */
    if ((cs == EL_UI_CS_INT || cs == EL_UI_CS_DECIMAL) && c == '-' && len != 0) return EL_ERR_ARG;
    if (cs == EL_UI_CS_DECIMAL && c == '.' && strchr(buf, '.')) return EL_ERR_ARG;
    size_t limit = cap - 1;
    if (max_chars && max_chars < limit) limit = max_chars;
    if (len >= limit) return EL_ERR_SIZE;
    buf[len] = c;
    buf[len + 1] = '\0';
    return EL_OK;
}

void el_ui_text_backspace(char *buf)
{
    if (!buf) return;
    size_t len = strlen(buf);
    if (len) buf[len - 1] = '\0';
}
