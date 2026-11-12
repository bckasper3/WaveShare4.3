# ESP32-S3 Environment Logger

A touchscreen temperature and relative-humidity data logger for the
**Waveshare ESP32-S3-Touch-LCD-4.3B** (800 x 480 touch display) and a Sensirion **SHT4x** or
**SHT3x** sensor. It measures on request or automatically at a fixed interval, keeps every
result in a crash-safe log on the internal flash, raises alarms on high/low limits, and gets
the data out over USB serial, RS-485 Modbus RTU, CSV files on a microSD card and (optionally)
a send-only HTTPS upload over Wi-Fi.

Status: firmware builds cleanly (ESP-IDF 5.5) and the logic is unit-tested on the PC; the UI is
reviewed in a PC simulator. Testing on real hardware is still to be done.

## Features
- **Readings**: temperature (degC), relative humidity (%RH), dew point. Each result averages
  1-10 sensor readings and stores their standard deviations.
- **Periodic logging**: every 10 s to 24 h (default 5 min), on a fixed grid; plus manual
  readings (screen button, USB command, Modbus command, digital input DI0).
- **Alarms**: high/low limits for temperature and humidity; ALARM status, screen wake-up and
  toast, DO1 alarm output, Modbus flags.
- **Calibration**: temperature and humidity offsets set against a reference instrument (Admin).
- **Crash-safe storage**: about 50,000 results in a power-cut-safe log; settings in two-slot NVS
  blobs. The supply can be cut at any moment.
- **Touch UI** (LVGL 8.3): Home, History, Trend (temperature / humidity charts with limit
  lines), Export, Calibration, Settings, Wi-Fi, Diagnostics, About; Menu on every screen, Back
  and Home in every header; light and dark theme; idle screen off.
- **Data out**: USB CDC serial (commands, CSV stream), Modbus RTU slave on RS-485, CSV export
  and a daily CSV mirror on microSD, Wi-Fi upload (off by default, send-only).
- **Access levels**: Operator / Admin / Engineer with PIN codes, lockout and idle logout.
- **Diagnostics**: self-test catalogue (boot, full, fixture), event log, diagnostic boot mode.
- **PC simulator** of the complete UI and **host unit tests** of all product logic.

## Hardware
- Waveshare ESP32-S3-Touch-LCD-4.3B (ESP32-S3-WROOM-1-N16R8).
- Sensirion SHT40/41/45 (default, I2C 0x44) or SHT30/31/35 (0x44 or 0x45) on the board's
  I2C terminal:

  | Sensor | Board terminal |
  |---|---|
  | VDD | 3V3 |
  | GND | GND |
  | SDA | SDA |
  | SCL | SCL |

  Select the sensor family and address in Settings > Logging & alarms (applies after a
  restart). Without a sensor the logger runs on a simulated sensor and marks results `SIM`.
- Optional: microSD card (FAT32), RS-485 wiring, DI/DO on the terminal block.
- Power: 7-36 V on the terminal block or 5 V on USB-C.

Details: [docs/HARDWARE.md](docs/HARDWARE.md).

## Build and flash (ESP-IDF 5.5.x)
The scripts expect ESP-IDF in `C:\esp\v5.5.6\esp-idf` (or set `ENVLOGGER_IDF_PATH`) and put the
build folder under `%LOCALAPPDATA%\envlogger`. Keep the checkout on a short path without spaces.

```powershell
powershell -File tools\idf.ps1 set-target esp32s3
powershell -File tools\idf.ps1 build
powershell -File tools\idf.ps1 -p COM5 flash monitor
```

The first build downloads LVGL and the Espressif components. After the firmware has taken the
USB port, flash with BOOT held while pressing RESET (or the `BOOTLOADER` serial command).
On Linux/macOS use `idf.py build` directly.

## PC simulator
The same UI sources, LVGL 8.3 and SDL2 (MinGW gcc + Ninja + CMake on Windows; downloaded on
first build):

```powershell
powershell -File tools\sim_shots.ps1          # build, run the navigation test, write screenshots
powershell -File tools\sim_shots.ps1 -Run     # interactive window (mouse = touch, S = SD card)
powershell -File tools\sim_shots.ps1 -Out docs\screenshots   # screenshots into a folder of your choice
```

Screenshots go to `%LOCALAPPDATA%\envlogger\shots` by default.

## Host unit tests
```powershell
powershell -File tools\host_test.ps1
```
Builds `components/envlogger_core` (pure C11, no ESP-IDF headers) with gcc and runs the Unity
tests in `test/host/tests` (sensor CRC and conversion, measurement state machine, periodic
logging, record codec, crash-safe log with power cuts at every byte, settings, Modbus engine,
CSV, JSON upload, UI formatting, ...).

## Serial commands (USB CDC)
| Command | Does |
|---|---|
| `*IDN?` | identity |
| `MEAS` / `CANCEL` | measure now (the result follows as a CSV row) / cancel |
| `LAST?`, `LOG? <n>`, `LOG COUNT?` | last result, last n results, count (CSV) |
| `STREAM ON\|OFF` | push every new result |
| `AUTO?`, `CAL?` | logging interval / next reading, calibration offsets |
| `SET?`, `SET <key> <value>` | list / change settings (e.g. `SET log_interval_s 600`) |
| `TIME?`, `TIME <ISO-8601>` | clock (UTC) |
| `LOGIN ADMIN\|ENGINEER <code>`, `LOGOUT`, `WHOAMI?` | access level |
| `SELFTEST [BOOT\|FULL\|<id>]`, `STATUS?`, `IO?`, `I2C SCAN` | diagnostics |

Full list, error codes and the CSV format: [docs/PROTOCOLS.md](docs/PROTOCOLS.md).

## Modbus RTU (summary)
Default address 1, 19200 baud, even parity. 32-bit values are two registers, high word first.

| Input reg | Content | Holding reg | Content |
|---|---|---|---|
| 0 | temperature f32 degC | 100 | command: 1 measure, 2 cancel, 3 clear alarm |
| 2 | status (0 none, 1 normal, 2 alarm, 3 error) | 101 | readings per result |
| 4 | timestamp u32 | 102 / 104 | low / high temperature alarm f32 |
| 6 | measurement state | 106 / 108 | low / high humidity alarm f32 |
| 7 | alarm bits | 110-112 | address, baud, parity |
| 10 | humidity f32 %RH | 113 | logging interval u32 s |
| 13 | dew point f32 | | |
| 26 | seconds to the next automatic reading u32 | | |
| 40 / 50 | serial number / tag (ASCII) | | |

Full map: [docs/PROTOCOLS.md](docs/PROTOCOLS.md) section 3.

## Default credentials
- Admin PIN: **1234**
- Engineer code: **1234**

**Change both after setup** (Settings > Security, Settings > Device, or `SET admin_pin ...` /
`SET engineer_code ...` over USB).

## Documentation
| File | Content |
|---|---|
| [docs/FIRMWARE_ARCH.md](docs/FIRMWARE_ARCH.md) | services, tasks, boot order |
| [docs/HARDWARE.md](docs/HARDWARE.md) | board, IO expander, sensor wiring, pins |
| [docs/MEASUREMENT.md](docs/MEASUREMENT.md) | measurement, alarms, periodic logging, calibration |
| [docs/PROTOCOLS.md](docs/PROTOCOLS.md) | serial commands, CSV, Modbus, digital I/O |
| [docs/APP.md](docs/APP.md) | controller, Modbus glue, digital I/O, console |
| [docs/STORAGE.md](docs/STORAGE.md), [docs/DATALOG.md](docs/DATALOG.md) | NVS, log, SD card, export |
| [docs/DIAGNOSTICS.md](docs/DIAGNOSTICS.md) | access levels, self-tests, boot modes, bring-up |
| [docs/USB.md](docs/USB.md), [docs/WIFI.md](docs/WIFI.md) | USB serial, send-only upload |
| [THIRD_PARTY.md](THIRD_PARTY.md) | third-party components and licences |
| [docs/Kasper_Backpack_REV8_dimensions.pdf](docs/Kasper_Backpack_REV8_dimensions.pdf) | outline and mounting drawing of an optional backpack board (not used by this firmware) |

Tools: `tools/gen_logo.py` (regenerates the logo), `main/ui/fonts/gen_fonts.py` (fonts),
`tools/check_clean.py` (repository word check).

## Licence
No licence has been chosen for this project's own code yet; until one is added, all rights are
reserved by the author. Third-party parts keep their own licences (see THIRD_PARTY.md).
