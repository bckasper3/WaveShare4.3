#pragma once
/* Boot mode (docs/APP.md section 1), implemented in main.c. */
#include "el_app_boot.h"

el_app_boot_mode_t app_boot_mode(void);
/* Leave the one-shot flag in RTC no-init RAM and restart. Does not return. */
void app_boot_request(el_app_boot_mode_t mode);
