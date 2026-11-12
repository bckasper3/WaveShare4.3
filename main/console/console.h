#pragma once
/* Serial command console: the el_cmd command table for the USB CDC port (docs/PROTOCOLS.md
 * section 1, docs/DIAGNOSTICS.md). Owned by the App service. Lines arrive from usb.h's RX
 * handler; each interface keeps its own el_session_t (login level). */
#include "esp_err.h"

esp_err_t console_init(void);   /* registers the USB RX handler, starts the console task */
