/* Device mode: TinyUSB CDC-ACM serial to a PC. Descriptors come from Kconfig
 * (TODO: production VID/PID; the Espressif test PID is for development only). */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "usb.h"
#include "usb_priv.h"
#include "app_state.h"
#include "app_events.h"

static const char *TAG = "usb_cdc";
static SemaphoreHandle_t s_tx_mtx;
static volatile bool s_up, s_dtr;
static usb_cdc_rx_fn s_rx_fn;
static void *s_rx_ctx;

void usb_cdc_set_rx_handler(usb_cdc_rx_fn fn, void *ctx) { s_rx_ctx = ctx; s_rx_fn = fn; }
bool usb_cdc_connected(void) { return s_up && s_dtr; }

static void on_rx(int itf, cdcacm_event_t *event)
{
    (void)event;
    uint8_t buf[64];
    size_t n = 0;
    while (tinyusb_cdcacm_read((tinyusb_cdcacm_itf_t)itf, buf, sizeof buf, &n) == ESP_OK && n) {
        if (s_rx_fn) s_rx_fn(buf, n, s_rx_ctx);
        if (n < sizeof buf) break;
    }
}

static void on_line_state(int itf, cdcacm_event_t *event)
{
    (void)itf;
    bool dtr = event->line_state_changed_data.dtr;
    if (dtr == s_dtr) return;
    s_dtr = dtr;
    app_state_lock();
    app_state_raw()->usb_pc_connected = dtr;
    app_state_unlock();
    app_events_post(dtr ? EVT_USB_PC_CONNECTED : EVT_USB_PC_DISCONNECTED, SRC_INTERNAL, 0);
}

esp_err_t usb_cdc_install(void)
{
    if (!s_tx_mtx) s_tx_mtx = xSemaphoreCreateMutex();
    if (s_up) return ESP_OK;
    const tinyusb_config_t tc = { 0 };            /* Kconfig descriptors, internal PHY */
    esp_err_t e = tinyusb_driver_install(&tc);
    if (e != ESP_OK) { ESP_LOGE(TAG, "tinyusb install: %s", esp_err_to_name(e)); return e; }
    tinyusb_config_cdcacm_t cc = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = on_rx,
        .callback_line_state_changed = on_line_state,
    };
    e = tusb_cdc_acm_init(&cc);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "CDC init: %s", esp_err_to_name(e));
        tinyusb_driver_uninstall();
        return e;
    }
    s_up = true;
    return ESP_OK;
}

void usb_cdc_uninstall(void)
{
    if (!s_up) return;
    xSemaphoreTake(s_tx_mtx, portMAX_DELAY);       /* no writer inside the stack */
    s_up = false;
    s_dtr = false;
    tusb_cdc_acm_deinit(TINYUSB_CDC_ACM_0);
    tinyusb_driver_uninstall();
    xSemaphoreGive(s_tx_mtx);
    app_state_lock();
    app_state_raw()->usb_pc_connected = false;
    app_state_unlock();
}

size_t usb_cdc_write(const char *data, size_t len)
{
    if (!usb_cdc_connected() || !data || !len) return 0;
    xSemaphoreTake(s_tx_mtx, portMAX_DELAY);
    size_t done = 0;
    if (s_up) {
        while (done < len) {
            size_t n = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)data + done, len - done);
            done += n;
            /* Flush; a PC that stopped reading makes this time out and the rest is dropped. */
            if (tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(50)) != ESP_OK && n == 0) break;
        }
    }
    xSemaphoreGive(s_tx_mtx);
    return done;
}
