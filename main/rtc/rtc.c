#include <sys/time.h>
#include "esp_log.h"
#include "i2c_bus.h"
#include "rtc.h"
#include "el_hw_pcf85063.h"

static const char *TAG = "rtc";
static i2c_master_dev_handle_t s_dev;
static bool s_valid;

esp_err_t rtc_get(time_t *utc)
{
    uint8_t r[EL_PCF85063_TIME_LEN];
    esp_err_t e = i2c_bus_read_regs(s_dev, EL_PCF85063_REG_TIME, r, sizeof r);   /* one burst */
    if (e != ESP_OK) return e;
    int64_t t = 0;
    bool os = true;
    el_err_t k = el_hw_pcf85063_decode(r, &t, &os);
    s_valid = (k == EL_OK) && !os;
    if (k != EL_OK) return ESP_ERR_INVALID_RESPONSE;
    if (utc) *utc = (time_t)t;
    return ESP_OK;
}

esp_err_t rtc_set(time_t utc)
{
    uint8_t b[1 + EL_PCF85063_TIME_LEN] = { EL_PCF85063_REG_TIME };
    if (el_hw_pcf85063_encode((int64_t)utc, &b[1]) != EL_OK) return ESP_ERR_INVALID_ARG;
    esp_err_t e = i2c_bus_write(s_dev, b, sizeof b);   /* clears OS */
    if (e == ESP_OK) {
        struct timeval tv = { .tv_sec = utc };
        settimeofday(&tv, NULL);
        s_valid = true;
    }
    return e;
}

esp_err_t rtc_clock_init(void)
{
    if (!s_dev) {
        esp_err_t e = i2c_bus_add(EL_PCF85063_ADDR, &s_dev);
        if (e != ESP_OK) return e;
    }
    /* 12.5 pF load, 24 h, running. Only written when different, so a running clock is
     * never touched at boot. */
    uint8_t c1 = 0;
    esp_err_t e = i2c_bus_read_regs(s_dev, EL_PCF85063_REG_CTRL1, &c1, 1);
    if (e != ESP_OK) { ESP_LOGE(TAG, "RTC not responding"); return e; }
    if (c1 != EL_PCF85063_CTRL1_INIT) {
        ESP_LOGW(TAG, "CTRL1 0x%02X -> 0x%02X", c1, EL_PCF85063_CTRL1_INIT);
        i2c_bus_write_reg8(s_dev, EL_PCF85063_REG_CTRL1, EL_PCF85063_CTRL1_INIT);
    }

    time_t now = 0;
    e = rtc_get(&now);
    if (e == ESP_OK && s_valid) {
        struct timeval tv = { .tv_sec = now };
        settimeofday(&tv, NULL);
    } else {
        ESP_LOGW(TAG, "RTC time not valid (OS flag or bad date): user must set the clock");
    }
    return e == ESP_ERR_INVALID_RESPONSE ? ESP_OK : e;   /* chip answered; time just invalid */
}

bool rtc_time_valid(void) { return s_valid; }
