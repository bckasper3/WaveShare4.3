# Firmware architecture (ESP-IDF side)

Product logic lives in `components/envlogger_core` (pure C11, no ESP-IDF / FreeRTOS headers,
host-tested on the PC). `main/` is the thin ESP-IDF layer: drivers, services, tasks and the UI.

## 1. Services and their public headers
| Service | Folder | Public header(s) | Owns |
|---|---|---|---|
| Board | `main/board/`, `main/rtc/` | `board.h`, `i2c_bus.h`, `display.h`, `rtc.h`, `sd_card.h` | I2C bus, CH422G, RGB panel + GT911 + LVGL port, RTC, SD card SPI/FAT mount |
| USB | `main/usb/` | `usb.h` | TinyUSB CDC-ACM device (PC serial) |
| Storage | `main/storage/`, `main/fwupdate/` | `storage.h`, `fw_update.h` | NVS blobs, internal FAT + result log, SD mirror / event log, CSV export, firmware update |
| App | `main/app/`, `main/sensor/`, `main/comms/`, `main/console/`, `main/net/`, `main.c` | `app_state.h`, `app_events.h`, `console.h`, `net.h` | controller (el_meas + el_autolog), SHT / simulated sensor, Modbus UART, DI/DO, serial commands, Wi-Fi upload, boot sequence |
| UI | `main/ui/` + `sim/` | `ui.h` | LVGL screens; builds for the ESP32 and for the PC simulator |

Rules:
- Call other services only through their public header.
- The UI never calls drivers. It reads `app_state_get()`, reads data through `storage.h`, sends
  requests with `app_events_post()` and uses `ui_port.h` (device: `ui_port_esp.c`, simulator:
  `sim/sim_backend.c`).
- Only the storage service touches files/NVS. Only the board service touches the CH422G.
- Each service has one `*_init()` called from `main.c` in the order of section 3, and exports
  its self-tests as an `el_test_def_t` table.
- Never block the LVGL task.

## 2. Tasks
| Task | Core | Prio | Stack | Period | Owner |
|---|---|---|---|---|---|
| LVGL (esp_lvgl_port) | 1 | 4 | 8 k | 5-10 ms | board/ui |
| app controller | 1 | 6 | 6 k | event + 20 ms tick (el_meas_step, el_autolog) | app |
| dio poll | 0 | 3 | 3 k | 50 ms | app |
| modbus uart | 0 | 5 | 4 k | UART event | app |
| console (serial lines) | 0 | 4 | 6 k | queue | app |
| net (Wi-Fi, off by default) | 0 | 3 | 6 k | on demand | app |
| storage (SD mirror, event log) | 0 | 2 | 4 k | 500 ms | storage |
| export / fw update | 0 | 2 | 6 k | on demand | storage |

## 3. Boot order (`main.c`)
1. `board_init()` - CH422G outputs off, backlight off, before anything else.
2. `app_state_init()`, `app_events_init()`.
3. `storage_init()` - settings, log recovery, SD mount.
4. RTC -> system time. 5. `display_init()` + `ui_init()` -> boot screen with logo.
6. Boot self-test (BOOT group of every table) -> result to app_state.
7. `usb_init()`, dio, modbus, Wi-Fi, console, controller (sensor probe + periodic logger).
8. `fw_update_confirm_boot()` only if the boot self-test has no FAIL in NVS / STORAGE / I2C_BUS.
Diagnostic boot (docs/DIAGNOSTICS.md section 3) stops after storage + USB console.

## 4. Storage media
Internal flash is the primary, crash-safe log (docs/DATALOG.md). The SD card is optional and
removable: CSV mirror of each result, an event/diagnostic log, the export target and the
firmware-update source. SD cards can corrupt themselves on a power cut, so nothing is stored
ONLY on the SD card. Details: docs/STORAGE.md.
