#pragma once
/* PCF85063A real-time clock on the Waveshare board (0x51), CR927 backup cell.
 * On boot: read RTC -> settimeofday() so time() is valid everywhere.
 * The RTC holds UTC; the user's offset is applied for display only.
 * Register codec: el_hw_pcf85063 (envlogger_core). */
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

esp_err_t rtc_clock_init(void);                 /* probe, check OS (oscillator stop) flag, sync system time */
bool      rtc_time_valid(void);           /* false if OS flag set / never set -> prompt user to set time */
/* ESP_OK with *utc filled when the chip answered and the block decoded (check rtc_time_valid()
 * for the OS flag). ESP_ERR_INVALID_RESPONSE for an impossible date. */
esp_err_t rtc_get(time_t *utc);
esp_err_t rtc_set(time_t utc);            /* also updates system time; 2000..2099 only */
