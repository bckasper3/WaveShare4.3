#pragma once
/* Boot-mode decision and boot self-test bookkeeping (docs/DIAGNOSTICS.md section 3,
 * docs/FIRMWARE_ARCH.md section 3, docs/APP.md section 1). Pure C. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_selftest.h"

typedef enum {
    EL_APP_BOOT_NORMAL = 0,   /* everything */
    EL_APP_BOOT_DIAG,         /* board_init, storage_init, usb_init + console; nothing automatic */
    EL_APP_BOOT_FACTORY,      /* DIAG + the full non-interactive self-test, report to the event log */
    EL_APP_BOOT_MODE_COUNT
} el_app_boot_mode_t;

typedef struct {
    bool bringup_build;            /* CONFIG_EL_BRINGUP */
    bool diag_flag;                /* one-shot flag left by "BOOT DIAG" (consumed by this boot) */
    bool fixture_detect_enabled;   /* CONFIG_EL_FIXTURE_DETECT (DI1 = loopback plug, VERIFY) */
    bool fixture_present;          /* DI1 active at power-up */
} el_app_boot_in_t;

/* First match: fixture (if detection enabled) -> FACTORY; bring-up build or flag -> DIAG;
 * else NORMAL. NULL -> NORMAL. */
el_app_boot_mode_t el_app_boot_decide(const el_app_boot_in_t *in);
const char        *el_app_boot_mode_name(el_app_boot_mode_t m);   /* "NORMAL", "DIAG", "FACTORY" */

/* One-shot flag word kept in RTC no-init RAM across a software reset (not across a power cut).
 * The word carries a magic, the mode and its complement, so the random RAM contents after a
 * power-up are never read as a request. */
#define EL_APP_BOOT_FLAG_MAGIC  0xB0D1u
uint32_t el_app_boot_flag_encode(el_app_boot_mode_t m);
/* true and *m set if w is a valid flag word for a non-NORMAL mode. */
bool     el_app_boot_flag_decode(uint32_t w, el_app_boot_mode_t *m);

/* ---- Boot self-test tally ----
 * fw_update_confirm_boot() is allowed only if none of the blocker tests FAILED: NVS, STORAGE,
 * I2C_BUS (FIRMWARE_ARCH.md step 8). A missing blocker test does not block. */
typedef struct {
    uint16_t pass, warn, fail, skip;
    bool     blocker_failed;
    char     first_fail[24];      /* id of the first FAIL, "" if none */
} el_app_tally_t;

bool el_app_boot_is_blocker(const char *test_id);
void el_app_tally_init(el_app_tally_t *t);
void el_app_tally_add(el_app_tally_t *t, const char *test_id, el_test_outcome_t o);
bool el_app_tally_may_confirm(const el_app_tally_t *t);
