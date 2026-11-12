#include <stddef.h>
#include "el_hw_ch422g.h"

void el_hw_ch422g_boot(el_hw_ch422g_t *s)
{
    /* DISP = 0 (backlight off until the first frame is drawn), TP_RST = 0 and LCD_RST = 0
     * (held in reset until display_init runs the GT911 strap sequence), SD_CS = 1. */
    s->io = (uint8_t)(1u << EL_EXIO_SD_CS);
    s->oc = EL_CH422G_OC_BOOT;
}

el_err_t el_hw_ch422g_io_set(el_hw_ch422g_t *s, uint8_t bit, bool level)
{
    if (!s || bit > 7 || (EL_CH422G_IO_DI_MASK & (1u << bit))) return EL_ERR_ARG;
    if (level) s->io = (uint8_t)(s->io | (1u << bit));
    else       s->io = (uint8_t)(s->io & ~(1u << bit));
    return EL_OK;
}

bool el_hw_ch422g_io_get(const el_hw_ch422g_t *s, uint8_t bit)
{
    return bit < 8 && ((s->io >> bit) & 1u);
}

uint8_t el_hw_ch422g_io_byte(const el_hw_ch422g_t *s)
{
    return (uint8_t)(s->io & (uint8_t)~EL_CH422G_IO_DI_MASK);
}

el_err_t el_hw_ch422g_oc_set(el_hw_ch422g_t *s, uint8_t oc_bit, bool on)
{
    if (!s || oc_bit > 3) return EL_ERR_ARG;
    /* Inverted: bit 0 lights the opto LED and turns the output transistor on. */
    if (on) s->oc = (uint8_t)(s->oc & ~(1u << oc_bit));
    else    s->oc = (uint8_t)(s->oc | (1u << oc_bit));
    s->oc = (uint8_t)(s->oc & EL_CH422G_OC_MASK);
    return EL_OK;
}

bool el_hw_ch422g_oc_is_on(const el_hw_ch422g_t *s, uint8_t oc_bit)
{
    return oc_bit < 4 && !((s->oc >> oc_bit) & 1u);
}

uint8_t el_hw_ch422g_oc_byte(const el_hw_ch422g_t *s) { return (uint8_t)(s->oc & EL_CH422G_OC_MASK); }

void el_hw_ch422g_di_decode(uint8_t raw, bool *di0_active, bool *di1_active)
{
    if (di0_active) *di0_active = !((raw >> EL_EXIO_DI0) & 1u);
    if (di1_active) *di1_active = !((raw >> EL_EXIO_DI1) & 1u);
}

bool el_hw_ch422g_readback_ok(const el_hw_ch422g_t *s, uint8_t raw)
{
    /* DI bits are excluded: we drive them 0, and an active opto also pulls them to 0, but a
     * read-back there proves nothing about the expander. */
    uint8_t m = (uint8_t)~EL_CH422G_IO_DI_MASK;
    return (uint8_t)(raw & m) == (uint8_t)(el_hw_ch422g_io_byte(s) & m);
}

uint8_t el_hw_ch422g_test_pattern(const el_hw_ch422g_t *s, unsigned n)
{
    uint8_t keep = (uint8_t)(el_hw_ch422g_io_byte(s) & (uint8_t)~EL_CH422G_IO_SPARE_MASK);
    return (uint8_t)(keep | (uint8_t)((n & 3u) << EL_EXIO_SPARE6));
}

static bool name_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char ca = (*a >= 'a' && *a <= 'z') ? (char)(*a - 32) : *a;
        if (ca != *b) return false;
    }
    return *a == '\0' && *b == '\0';
}

el_err_t el_hw_ch422g_parse_name(const char *name, el_hw_io_kind_t *kind, uint8_t *bit)
{
    static const struct { const char *n; el_hw_io_kind_t k; uint8_t b; } map[] = {
        { "DO0",     EL_HW_IO_OC,   EL_OC_DO0 },
        { "DO1",     EL_HW_IO_OC,   EL_OC_DO1 },
        { "BL",      EL_HW_IO_EXIO, EL_EXIO_DISP },
        { "LCD_RST", EL_HW_IO_EXIO, EL_EXIO_LCD_RST },
        { "TP_RST",  EL_HW_IO_EXIO, EL_EXIO_TP_RST },
    };
    if (!name || !kind || !bit) return EL_ERR_ARG;
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
        if (name_eq(name, map[i].n)) { *kind = map[i].k; *bit = map[i].b; return EL_OK; }
    }
    return EL_ERR_ARG;
}
