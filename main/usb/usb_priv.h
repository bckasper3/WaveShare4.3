#pragma once
/* USB service internals: the CDC device stack (usb_cdc.c), installed once by usb.c. */
#include <stdbool.h>
#include "esp_err.h"

esp_err_t usb_cdc_install(void);
void      usb_cdc_uninstall(void);
