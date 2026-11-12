/* Sensor service: SHT4x / SHT3x (el_sensor_sht) when one answers at boot, otherwise the
 * simulated sensor (results flagged SIM). The SHT driver is not thread-safe, so every call to it
 * goes through one mutex: the controller task's ops (wrapped below) and the self-test (SELFTEST
 * command, Diagnostics screen) never interleave. */
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "sensor.h"
#include "i2c_bus.h"
#include "storage.h"
#include "el_sensor_sht.h"

static const char *TAG = "sensor";

static el_sensor_sim_t  s_sim;
static el_sht_t         s_sht;
static SemaphoreHandle_t s_lock;
static i2c_master_dev_handle_t s_dev;
static bool s_inited, s_real, s_use_sim = true;

/* ---------------- I2C transport for el_sensor_sht ---------------- */

static int io_write(void *bus, uint8_t addr, const uint8_t *d, size_t n)
{
    (void)bus; (void)addr;                      /* s_dev was added for the sensor's address */
    return i2c_bus_write(s_dev, d, n) == ESP_OK ? 0 : -1;
}

static int io_read(void *bus, uint8_t addr, uint8_t *d, size_t n)
{
    (void)bus; (void)addr;
    return i2c_bus_read(s_dev, d, n) == ESP_OK ? 0 : -1;
}

/* ---------------- locked wrapper around the SHT driver ---------------- */

static void lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

static el_sensor_err_t w_begin(void *ctx, uint32_t now)
{
    lock();
    el_sensor_err_t e = el_sht_driver(ctx).ops->begin_conversion(ctx, now);
    unlock();
    return e;
}
static el_sensor_poll_t w_poll(void *ctx, uint32_t now, el_sensor_sample_t *s, el_sensor_err_t *err)
{
    lock();
    el_sensor_poll_t p = el_sht_driver(ctx).ops->poll(ctx, now, s, err);
    unlock();
    return p;
}
static void w_abort(void *ctx)
{
    lock();
    el_sht_driver(ctx).ops->abort(ctx);
    unlock();
}
static el_err_t w_info(void *ctx, char *buf, size_t cap)
{
    lock();
    el_err_t e = el_sht_driver(ctx).ops->info(ctx, buf, cap);
    unlock();
    return e;
}
static void w_caps(void *ctx, el_sensor_caps_t *out)
{
    lock();
    el_sht_driver(ctx).ops->caps(ctx, out);
    unlock();
}
static const el_sensor_ops_t LOCKED_SHT = { w_begin, w_poll, w_abort, w_info, w_caps };

/* ---------------- service ---------------- */

/* One blocking reading (lock held by the caller). */
static el_sensor_err_t read_blocking(el_sensor_sample_t *s)
{
    el_sensor_t d = el_sht_driver(&s_sht);
    uint32_t t0 = (uint32_t)(esp_timer_get_time() / 1000);
    el_sensor_err_t e = d.ops->begin_conversion(d.ctx, t0);
    if (e != EL_SENSOR_OK) return e;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5));
        el_sensor_err_t err = EL_SENSOR_OK;
        el_sensor_poll_t p = d.ops->poll(d.ctx, (uint32_t)(esp_timer_get_time() / 1000), s, &err);
        if (p == EL_SENSOR_READY) return EL_SENSOR_OK;
        if (p == EL_SENSOR_ERROR) return err;
    }
}

static bool probe_sht(void)
{
    el_settings_t set;
    storage_settings_get(&set);
    el_sht_model_t model = set.sensor_type == EL_SENSOR_SHT3X ? EL_SHT3X : EL_SHT4X;
    uint8_t addr = set.sensor_addr == EL_SENSOR_ADDR_45 ? EL_SHT_ADDR_ALT : EL_SHT_ADDR_DEFAULT;
    const el_sht_io_t io = { io_write, io_read, NULL };
    if (el_sht_init(&s_sht, model, addr, &io) != EL_OK) return false;
    if (i2c_bus_probe(addr) != ESP_OK) return false;             /* nothing there */
    if (!s_dev && i2c_bus_add(addr, &s_dev) != ESP_OK) return false;
    size_t n = 0;
    const uint8_t *rst = el_sht_reset_cmd(model, &n);
    i2c_bus_write(s_dev, rst, n);                                /* soft reset, then 1 ms */
    vTaskDelay(pdMS_TO_TICKS(2));
    el_sensor_sample_t s;
    lock();
    el_sensor_err_t e = read_blocking(&s);
    unlock();
    if (e != EL_SENSOR_OK) {
        ESP_LOGW(TAG, "device at 0x%02X does not answer like an %s (%s)", addr, el_sht_model_name(model),
                 el_sensor_err_name(e));
        return false;
    }
    return true;
}

esp_err_t sensor_init(void)
{
    if (s_inited) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    el_sensor_sim_cfg_t cfg;
    el_sensor_sim_default_cfg(&cfg);
    cfg.noise_temp_sd = 0.02f;            /* about what an SHT4x shows at high repeatability */
    cfg.noise_rh_sd = 0.08f;
    cfg.seed = esp_random();
    if (el_sensor_sim_init(&s_sim, &cfg) != EL_OK) return ESP_FAIL;

    s_real = probe_sht();
    s_use_sim = !s_real;
    s_inited = true;
    char info[32] = "";
    el_sht_driver(&s_sht).ops->info(&s_sht, info, sizeof info);
    if (s_real) ESP_LOGI(TAG, "%s", info);
    else ESP_LOGW(TAG, "no %s: using the SIMULATED sensor (results flagged SIM)", info);
    return ESP_OK;
}

el_sensor_t sensor_driver(void)
{
    if (s_use_sim) return el_sensor_sim_driver(&s_sim);
    el_sensor_t d = { &LOCKED_SHT, &s_sht };
    return d;
}
bool sensor_present(void)   { return s_real; }
bool sensor_simulated(void) { return s_use_sim; }

esp_err_t sensor_set_simulation(bool on)
{
    if (!on && !s_real) return ESP_ERR_NOT_SUPPORTED;
    s_use_sim = on;
    return ESP_OK;
}

esp_err_t sensor_get_info(char *buf, size_t len)
{
    el_sensor_t d = sensor_driver();
    return d.ops->info(d.ctx, buf, len) == EL_OK ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

/* ---------------- self-test ---------------- */

static void t_sensor(void *ctx, el_test_report_t *r)
{
    (void)ctx;
    if (!s_real) {
        el_test_set(r, EL_TEST_WARN, NAN, "no SHT sensor answered: simulated sensor");
        return;
    }
    lock();
    if (s_sht.busy) {
        unlock();
        el_test_set(r, EL_TEST_SKIP, NAN, "measurement running");
        return;
    }
    el_sensor_sample_t s;
    el_sensor_err_t e = read_blocking(&s);
    unlock();
    if (e != EL_SENSOR_OK) {
        el_test_set(r, EL_TEST_FAIL, NAN, "%s @0x%02X: %s", el_sht_model_name(s_sht.model), s_sht.addr,
                    el_sensor_err_name(e));
        return;
    }
    el_test_set(r, EL_TEST_PASS, s.temp_c, "%s @0x%02X: %.1f C %.1f %%RH", el_sht_model_name(s_sht.model),
                s_sht.addr, (double)s.temp_c, (double)s.rh_pct);
}

static const el_test_def_t TESTS[] = {
    { "SENSOR", "Sensor reading with CRC check", "C", "I2C_BUS", EL_TG_BOOT, t_sensor },
};

const el_test_def_t *sensor_selftests(size_t *count)
{
    if (count) *count = sizeof TESTS / sizeof TESTS[0];
    return TESTS;
}
