# USB

The ESP32-S3's USB-OTG controller is wired straight to the board's USB-C connector
(GPIO19/20). The firmware runs it in **device mode only**: TinyUSB CDC-ACM, a virtual serial
port for the console and the result stream (docs/PROTOCOLS.md section 1).

- Descriptors come from `sdkconfig.defaults` (`CONFIG_TINYUSB_DESC_*`). The default VID/PID is
  Espressif's test ID, fine for development; use your own before shipping devices.
- The PC side needs no driver (Windows 10+, Linux, macOS).
- `app_state.usb_pc_connected` follows DTR (a terminal program has the port open).
- Flashing: once the application owns the USB PHY, the automatic esptool reset no longer works.
  Use the `BOOTLOADER` command (Engineer) or hold BOOT while pressing RESET.
- The ESP-IDF log console is disabled (UART0 is RS-485); diagnostic text goes through the CDC
  port and the event log.
