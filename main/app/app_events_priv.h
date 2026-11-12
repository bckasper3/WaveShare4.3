#pragma once
/* Used only by the controller task (app_controller.c). */
#include <stdbool.h>
#include <stdint.h>
#include "app_events.h"

bool app_events_wait(app_event_t *out, uint32_t timeout_ms);
void app_events_dispatch(const app_event_t *e);
