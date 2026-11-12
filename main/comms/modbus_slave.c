#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "modbus_slave.h"
#include "app_state.h"
#include "app_events.h"
#include "storage.h"
#include "sensor.h"
#include "app_config.h"
#include "el_app_modbus.h"

#define MB_UART      UART_NUM_0
#define MB_BUF       512
#define REFRESH_MS   250

static const char *TAG = "modbus";
static el_mb_t s_mb;
static QueueHandle_t s_evq;

static el_mb_verdict_t gate(void *ctx, const el_mb_effects_t *fx)
{
    (void)ctx;
    app_state_t st;
    app_state_get(&st);
    bool busy = st.meas_state == EL_MEAS_MEASURING;
    return el_app_mb_gate(fx, busy, storage_export_busy());
}

static void refresh(void)
{
    static app_state_t st;            /* big: keep it off the task stack */
    static el_settings_t set;
    app_state_get(&st);
    storage_settings_get(&set);

    el_app_mb_src_t src = {
        .has_result = st.has_result,
        .last = st.last_result,
        .result_counter = st.result_counter,
        .meas_state = (uint8_t)st.meas_state,
        .progress_pct = st.meas_progress_pct,
        .sensor_fault = st.last_fault != EL_MEAS_FAULT_NONE,
        .simulated = st.sensor_simulated,
        .log_near_full = st.log_near_full,
        .export_busy = st.export_busy,
        .sd_present = st.sd_present,
        .next_auto_s = st.next_auto_s,
        .uptime_s = st.uptime_s,
        .log_count = st.log_count,
        .log_fill_pct = st.log_fill_pct,
        .fw_version = FW_VERSION_STR,
        .serial = set.device_serial,
    };
    el_mb_inputs_t in;
    el_app_mb_inputs(&src, &in);
    el_mb_set_inputs(&s_mb, &in);

    el_mb_holding_t h;
    el_app_mb_holding(&set, &h);
    el_mb_set_holding(&s_mb, &h);
}

static void apply_effects(void)
{
    el_mb_effects_t fx;
    if (!el_mb_take_effects(&s_mb, &fx)) return;

    if (fx.mask & EL_MB_FX_COMMAND) {
        switch (fx.command) {
        case EL_MB_CMD_START:       app_events_post(EVT_REQ_MEASURE, SRC_MODBUS, 0); break;
        case EL_MB_CMD_CANCEL:      app_events_post(EVT_REQ_CANCEL, SRC_MODBUS, 0); break;
        case EL_MB_CMD_CLEAR_ALARM: app_events_post(EVT_REQ_CLEAR_ALARM, SRC_MODBUS, 0); break;
        default: break;
        }
    }

    el_settings_t cur;
    storage_settings_get(&cur);
    el_app_set_op_t ops[10];
    size_t n = el_app_mb_plan(&fx, &cur, ops, sizeof ops / sizeof ops[0]);
    for (size_t i = 0; i < n; i++) {
        el_err_t e = storage_settings_set(ops[i].id, ops[i].text, EL_LEVEL_ADMIN);
        if (e != EL_OK) ESP_LOGW(TAG, "write %s=%s rejected: %s",
                                 el_settings_def(ops[i].id)->key, ops[i].text, el_err_name(e));
    }
    refresh();      /* show the truth again (a rejected write snaps back) */
}

static void modbus_task(void *arg)
{
    (void)arg;
    static uint8_t frame[EL_MB_RTU_MAX_FRAME + 16];
    static uint8_t resp[EL_MB_RTU_MAX_FRAME];
    size_t len = 0;
    bool overflow = false;
    TickType_t last = 0;
    for (;;) {
        uart_event_t ev;
        if (xQueueReceive(s_evq, &ev, pdMS_TO_TICKS(REFRESH_MS))) {
            switch (ev.type) {
            case UART_DATA: {
                size_t n = ev.size;
                if (len + n > sizeof frame) { overflow = true; n = sizeof frame - len; }
                if (n) len += (size_t)uart_read_bytes(MB_UART, frame + len, n, 0);
                if (ev.timeout_flag) {            /* t3.5 of silence: frame complete */
                    if (!overflow && len) {
                        size_t r = el_mb_rtu_process(&s_mb, frame, len, resp, sizeof resp);
                        if (r) uart_write_bytes(MB_UART, resp, r);
                        apply_effects();
                    }
                    len = 0;
                    overflow = false;
                }
                break;
            }
            case UART_FIFO_OVF:
            case UART_BUFFER_FULL:
                uart_flush_input(MB_UART);
                xQueueReset(s_evq);
                len = 0;
                overflow = false;
                break;
            default:
                break;
            }
        }
        if (xTaskGetTickCount() - last >= pdMS_TO_TICKS(REFRESH_MS)) {
            last = xTaskGetTickCount();
            refresh();
        }
    }
}

esp_err_t modbus_slave_start(void)
{
    el_settings_t set;
    storage_settings_get(&set);
    uint32_t baud = el_settings_mb_baud_bps(&set);
    if (el_mb_init(&s_mb, set.mb_addr) != EL_OK) el_mb_init(&s_mb, 1);
    el_mb_set_gate(&s_mb, gate, NULL);
    refresh();

    uart_config_t uc = {
        .baud_rate = (int)baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = set.mb_parity == EL_PARITY_EVEN ? UART_PARITY_EVEN :
                  set.mb_parity == EL_PARITY_ODD ? UART_PARITY_ODD : UART_PARITY_DISABLE,
        .stop_bits = set.mb_parity == EL_PARITY_NONE ? UART_STOP_BITS_2 : UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t e = uart_driver_install(MB_UART, MB_BUF, MB_BUF, 16, &s_evq, 0);
    if (e == ESP_OK) e = uart_param_config(MB_UART, &uc);
    /* GPIO43 is U0TXD by default; this board wires it to the transceiver's RO. */
    if (e == ESP_OK) e = uart_set_pin(MB_UART, PIN_RS485_TX, PIN_RS485_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (e == ESP_OK) e = uart_set_rx_timeout(MB_UART, el_app_mb_rx_tout_chars(baud));
    if (e != ESP_OK) { ESP_LOGE(TAG, "UART setup: %s", esp_err_to_name(e)); return e; }

    if (xTaskCreatePinnedToCore(modbus_task, "modbus", 4096, NULL, 5, NULL, 0) != pdPASS)
        return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "slave %u, %lu baud, parity %u", s_mb.address, (unsigned long)baud, set.mb_parity);
    return ESP_OK;
}
