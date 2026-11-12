#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "app_state.h"

static app_state_t s_state;
static SemaphoreHandle_t s_lock;

void app_state_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_lock = xSemaphoreCreateMutex();
}

void app_state_lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
void app_state_unlock(void) { xSemaphoreGive(s_lock); }
app_state_t *app_state_raw(void) { return &s_state; }

void app_state_get(app_state_t *out)
{
    app_state_lock();
    *out = s_state;
    app_state_unlock();
}
