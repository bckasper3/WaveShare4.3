/* ESP32-S3 Environment Logger - firmware entry point.
 * Boot order: docs/FIRMWARE_ARCH.md section 3. Boot modes: docs/APP.md section 1.
 * The IO expander outputs are put into a safe state before anything else. */
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "app_config.h"
#include "app_state.h"
#include "app_events.h"
#include "app_boot.h"
#include "app_selftest.h"
#include "board.h"
#include "display.h"
#include "rtc.h"
#include "storage.h"
#include "usb.h"
#include "dio.h"
#include "modbus_slave.h"
#include "console.h"
#include "fw_update.h"
#include "ui.h"
#include "net.h"
#include "el_app_boot.h"

static const char *TAG = "main";

#ifdef CONFIG_EL_BRINGUP
#define BRINGUP_BUILD 1
#else
#define BRINGUP_BUILD 0
#endif
#ifdef CONFIG_EL_FIXTURE_DETECT
#define FIXTURE_DETECT 1
#else
#define FIXTURE_DETECT 0
#endif

/* Survives a software reset, not a power cut (el_app_boot flag word carries a magic). */
static RTC_NOINIT_ATTR uint32_t s_boot_flag;
static el_app_boot_mode_t s_mode;

el_app_boot_mode_t app_boot_mode(void) { return s_mode; }

void app_boot_request(el_app_boot_mode_t mode)
{
    s_boot_flag = el_app_boot_flag_encode(mode);
    esp_restart();
}

static el_app_boot_mode_t decide_mode(void)
{
    el_app_boot_mode_t flagged;
    bool flag = el_app_boot_flag_decode(s_boot_flag, &flagged) && flagged == EL_APP_BOOT_DIAG;
    s_boot_flag = 0;                                   /* one-shot: consumed now */
    bool di1 = false;
    if (FIXTURE_DETECT) board_read_di(NULL, &di1);
    el_app_boot_in_t in = {
        .bringup_build = BRINGUP_BUILD,
        .diag_flag = flag,
        .fixture_detect_enabled = FIXTURE_DETECT,
        .fixture_present = di1,
    };
    return el_app_boot_decide(&in);
}

static void boot_sink(void *user, const el_test_def_t *t, const el_test_report_t *r)
{
    el_app_tally_add((el_app_tally_t *)user, t->id, r->outcome);
    if (r->outcome == EL_TEST_FAIL || r->outcome == EL_TEST_WARN) {
        char line[192];
        el_selftest_format(line, sizeof line, t, r);
        ESP_LOGW(TAG, "%s", line);
        storage_event_log("%s", line);
    }
}

static void ui_listener(const app_event_t *e, void *ctx) { (void)ctx; ui_notify(e->id, e->arg); }

void app_main(void)
{
    /* 1. DO0/DO1 off, backlight off: before anything else. */
    esp_err_t be = board_init();
    setenv("TZ", "UTC0", 1);                 /* the RTC holds UTC; the UI applies the offset */
    tzset();

    /* 2. Shared state and the event bus. */
    app_state_init();
    app_events_init();

    s_mode = decide_mode();
    ESP_LOGI(TAG, "%s %s, boot mode %s%s", FW_VERSION_STR, BOARD_REV_STR, el_app_boot_mode_name(s_mode),
             be == ESP_OK ? "" : " (CH422G not answering!)");

    /* 3. Storage: settings, log recovery. Never fails hard. */
    storage_boot_report_t rep;
    storage_init(&rep);
    storage_event_log("boot %s mode %s", FW_VERSION_STR, el_app_boot_mode_name(s_mode));

    /* 4. Clock. */
    rtc_clock_init();
    app_state_lock();
    app_state_raw()->rtc_valid = rtc_time_valid();
    app_state_unlock();

    if (s_mode == EL_APP_BOOT_NORMAL) {
        /* 5. Display and UI (boot screen). */
        if (display_init() == ESP_OK) ui_init();
    }

    /* 6. Boot self-test (BOOT group of every table). */
    el_app_tally_t tally;
    el_app_tally_init(&tally);
    el_test_summary_t sum;
    app_selftest_run_group(EL_TG_BOOT, boot_sink, &tally, &sum);
    if (s_mode == EL_APP_BOOT_FACTORY) {
        el_app_tally_t full;
        el_app_tally_init(&full);
        app_selftest_run_group(EL_TG_FULL | EL_TG_FIXTURE, boot_sink, &full, &sum);
        storage_event_log("factory self-test: pass=%u warn=%u fail=%u skip=%u first_fail=%s",
                          full.pass, full.warn, full.fail, full.skip, full.first_fail);
    }
    app_state_lock();
    app_state_raw()->boot_selftest_fail = tally.fail;
    app_state_unlock();
    ESP_LOGI(TAG, "boot self-test: pass=%u warn=%u fail=%u skip=%u", tally.pass, tally.warn, tally.fail, tally.skip);

    /* 7. Services. DIAG/FACTORY: USB console only, nothing automatic. */
    usb_init();
    if (s_mode == EL_APP_BOOT_NORMAL) {
        dio_start();
        modbus_slave_start();
        net_start();                          /* Wi-Fi stays off unless an Admin enabled it */
        app_events_add_listener(ui_listener, NULL);
    }
    console_init();                           /* listeners before the controller starts */
    app_controller_start();
    /* Boot checks done: the UI leaves the logo (for Set time if the clock is not set). */
    if (s_mode == EL_APP_BOOT_NORMAL && display_ready()) ui_boot_done();

    /* 8. Accept a new OTA image only if no blocker test (NVS, STORAGE, I2C_BUS) failed. */
    if (el_app_tally_may_confirm(&tally)) fw_update_confirm_boot();
    else storage_event_log("boot self-test blocker failed (%s): firmware NOT confirmed", tally.first_fail);

    /* Housekeeping: uptime and the log counters every second. */
    for (;;) {
        uint32_t count;
        uint8_t fill;
        bool near, full;
        storage_log_status(&count, &fill, &near, &full);
        app_state_lock();
        app_state_t *st = app_state_raw();
        st->uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
        st->log_count = count;
        st->log_fill_pct = fill;
        st->log_near_full = near;
        st->log_full = full;
        st->export_busy = storage_export_busy();
        st->sd_present = storage_sd_present();
        app_state_unlock();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
