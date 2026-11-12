#pragma once
/* USB service: the ESP32-S3 USB-OTG controller on the board's USB-C, always in device mode as a
 * CDC-ACM serial port to a PC (docs/USB.md). The ESP-IDF console is NOT routed here;
 * diagnostic text goes through usb_cdc_write(). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "el_selftest.h"

/* Install TinyUSB + CDC-ACM. */
esp_err_t usb_init(void);

/* Bytes received from the PC are delivered to this callback from the USB task (keep it short:
 * feed an el_line_t and hand complete lines to the console task). */
typedef void (*usb_cdc_rx_fn)(const uint8_t *data, size_t len, void *ctx);
void   usb_cdc_set_rx_handler(usb_cdc_rx_fn fn, void *ctx);
bool   usb_cdc_connected(void);                    /* port open (DTR) */
size_t usb_cdc_write(const char *data, size_t len);   /* non-blocking-ish: drops if not connected */

/* Self-tests: none today (the CDC port is checked by talking to it). */
const el_test_def_t *usb_selftests(size_t *count);

/* Reboot into the ROM USB download mode (serial BOOTLOADER command, ENGINEER). */
void usb_reboot_to_bootloader(void);
