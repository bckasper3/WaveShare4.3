#pragma once
/* Modbus RTU slave on RS-485 (UART0, RX GPIO43 / TX GPIO44, auto-direction transceiver that
 * does not hear its own transmission: HARDWARE.md 7). The protocol engine is
 * el_modbus and the device glue el_app_modbus (both host-tested); this task only moves frames:
 * a frame ends at the UART RX timeout (t3.5), goes through el_mb_rtu_process, the reply goes
 * out, and accepted writes become events / settings writes (ADMIN level).
 * Address, baud and parity changes are stored at once and take effect after a reboot. */
#include "esp_err.h"

esp_err_t modbus_slave_start(void);
