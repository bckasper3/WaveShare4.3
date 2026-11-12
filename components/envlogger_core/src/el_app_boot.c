#include <string.h>
#include "el_app_boot.h"
#include "el_util.h"

el_app_boot_mode_t el_app_boot_decide(const el_app_boot_in_t *in)
{
    if (!in) return EL_APP_BOOT_NORMAL;
    if (in->fixture_detect_enabled && in->fixture_present) return EL_APP_BOOT_FACTORY;
    if (in->bringup_build || in->diag_flag) return EL_APP_BOOT_DIAG;
    return EL_APP_BOOT_NORMAL;
}

const char *el_app_boot_mode_name(el_app_boot_mode_t m)
{
    switch (m) {
    case EL_APP_BOOT_NORMAL:  return "NORMAL";
    case EL_APP_BOOT_DIAG:    return "DIAG";
    case EL_APP_BOOT_FACTORY: return "FACTORY";
    default:                  return "?";
    }
}

uint32_t el_app_boot_flag_encode(el_app_boot_mode_t m)
{
    uint8_t v = (uint8_t)m;
    return ((uint32_t)EL_APP_BOOT_FLAG_MAGIC << 16) | ((uint32_t)v << 8) | (uint8_t)~v;
}

bool el_app_boot_flag_decode(uint32_t w, el_app_boot_mode_t *m)
{
    if ((w >> 16) != EL_APP_BOOT_FLAG_MAGIC) return false;
    uint8_t v = (uint8_t)(w >> 8), inv = (uint8_t)w;
    if ((uint8_t)~v != inv) return false;
    if (v == EL_APP_BOOT_NORMAL || v >= EL_APP_BOOT_MODE_COUNT) return false;
    if (m) *m = (el_app_boot_mode_t)v;
    return true;
}

bool el_app_boot_is_blocker(const char *id)
{
    static const char *const B[] = { "NVS", "STORAGE", "I2C_BUS" };
    if (!id) return false;
    for (size_t i = 0; i < sizeof B / sizeof B[0]; i++) if (strcmp(id, B[i]) == 0) return true;
    return false;
}

void el_app_tally_init(el_app_tally_t *t)
{
    if (t) memset(t, 0, sizeof *t);
}

void el_app_tally_add(el_app_tally_t *t, const char *id, el_test_outcome_t o)
{
    if (!t) return;
    switch (o) {
    case EL_TEST_PASS: t->pass++; break;
    case EL_TEST_WARN: t->warn++; break;
    case EL_TEST_SKIP: t->skip++; break;
    default:
        t->fail++;                              /* FAIL, and any out-of-range outcome */
        if (!t->first_fail[0] && id) el_strlcpy(t->first_fail, id, sizeof t->first_fail);
        if (el_app_boot_is_blocker(id)) t->blocker_failed = true;
        break;
    }
}

bool el_app_tally_may_confirm(const el_app_tally_t *t)
{
    return t && !t->blocker_failed;
}
