#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "app_selftest.h"
#include "board.h"
#include "storage.h"
#include "usb.h"
#include "sensor.h"
#include "el_app_selftest.h"

static const char *TAG = "selftest";
static el_test_def_t s_tab[EL_SELFTEST_MAX];
static size_t s_n;
static bool s_built;
static SemaphoreHandle_t s_run_mtx;

static void build(void)
{
    if (s_built) return;
    el_app_test_table_t t[4];
    t[0].defs = board_selftests(&t[0].n);     /* I2C_BUS first: others depend on it */
    t[1].defs = storage_selftests(&t[1].n);
    t[2].defs = usb_selftests(&t[2].n);
    t[3].defs = sensor_selftests(&t[3].n);
    el_app_merge_report_t rep;
    el_app_selftest_merge(t, 4, s_tab, EL_SELFTEST_MAX, &s_n, &rep);
    if (rep.duplicates || rep.deps_cleared || rep.invalid || rep.truncated)
        ESP_LOGW(TAG, "merge: %u duplicate, %u deps cleared, %u invalid%s", rep.duplicates,
                 rep.deps_cleared, rep.invalid, rep.truncated ? ", truncated" : "");
    s_run_mtx = xSemaphoreCreateMutex();
    s_built = true;
}

const el_test_def_t *app_selftest_table(size_t *n)
{
    build();
    if (n) *n = s_n;
    return s_tab;
}

el_err_t app_selftest_run_group(uint32_t mask, el_test_sink_fn sink, void *user, el_test_summary_t *sum)
{
    build();
    xSemaphoreTake(s_run_mtx, portMAX_DELAY);
    el_err_t e = el_selftest_run(s_tab, s_n, mask, NULL, sink, user, sum);
    xSemaphoreGive(s_run_mtx);
    return e;
}

el_err_t app_selftest_run_id(const char *id, el_test_sink_fn sink, void *user, el_test_summary_t *sum)
{
    build();
    static el_test_def_t sel[EL_SELFTEST_MAX];
    size_t n = 0;
    xSemaphoreTake(s_run_mtx, portMAX_DELAY);
    el_err_t e = el_app_selftest_select(s_tab, s_n, id, sel, EL_SELFTEST_MAX, &n);
    if (e == EL_OK) e = el_selftest_run(sel, n, EL_TG_ALL, NULL, sink, user, sum);
    xSemaphoreGive(s_run_mtx);
    return e;
}
