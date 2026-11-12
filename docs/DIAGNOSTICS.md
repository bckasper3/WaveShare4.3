# Diagnostics, access levels and bring-up

## 1. Principles
1. **A diagnostic never depends on the thing it tests.** The USB serial console needs only the
   ESP32 and its USB: it works with a dead display, a stuck I2C bus or no sensor.
2. **One test catalogue, several front ends.** Every test is one entry in a table
   (`el_selftest`). The serial console, the Diagnostics screen and the boot check run the same
   entries.
3. **Machine-readable output:** `TEST <ID> <PASS|WARN|FAIL|SKIP> <value> <unit> <detail>`.
   Test IDs are stable.
4. **Dependencies are explicit.** If `I2C_BUS` fails, `RTC`, `TOUCH`, `SENSOR` report
   `SKIP needs I2C_BUS` instead of a wall of failures. The first FAIL is the real one.
5. **Nothing automatic in diagnostic mode.** Modbus, Wi-Fi, periodic logging and the
   measurement task stay stopped.

## 2. Access levels
One permission table (`el_access`) for the touchscreen and the serial port.

| Level | Can do (cumulative) | How to get in |
|---|---|---|
| **Operator** | measure, view history and trend, export, view diagnostics | default at power-up |
| **Admin** | + settings, alarm limits, logging interval, clock, calibration offsets, delete the log, firmware update, Admin PIN | Admin PIN (setting `admin_pin`, default **1234**; empty = no PIN) |
| **Engineer** | + raw I/O, raw I2C, simulation on/off, serial number, engineer code, factory reset, diagnostic boot | engineer code (setting `engineer_code`, default **1234**) |

- Each interface has its own session; idle timeout (`login_idle_s`, default 5 min) drops back
  to Operator; power-up is always Operator.
- Wrong codes: 5 tries, then a 30 s lockout that doubles on each further failure, up to 15 min.
- **Change both default codes after setup.** A forgotten code can always be recovered by
  re-flashing over USB (ROM bootloader), which erases nothing but the app, or by erasing NVS.

### 2.1 Login wrapper
`el_app_login_verdict` replays "is this code right?" through the session so the lockout and
timing rules are the same for every code scheme.

## 3. Boot modes
| Mode | What starts | Entered by |
|---|---|---|
| Normal | everything | default |
| Diagnostic | storage + USB serial console only | `BOOT DIAG` (Engineer, one-shot flag in RTC RAM), or the `CONFIG_EL_BRINGUP` build option |
| Factory test | diagnostic + the full self-test, report to the event log | DI1 active at power-up, only with `CONFIG_EL_FIXTURE_DETECT` |

On a normal boot the quick self-test (BOOT group) runs in about a second. Failures are shown on
the screen and logged; a new firmware image is only confirmed (OTA rollback cancelled) when
NVS, STORAGE and I2C_BUS did not fail.

## 4. Bring-up
1. Flash, open the USB serial port, `*IDN?`, `SELFTEST LIST`, `SELFTEST`.
2. `I2C SCAN`: CH422G, PCF85063, GT911 must answer; the SHT sensor at 0x44 / 0x45.
3. `TIME 2026-01-01T00:00:00Z`, power-cycle, `TIME?`: the RTC keeps time.
4. Display and touch: the boot screen and Home appear; tap through the Menu.
5. `MEAS` with the sensor connected: a CSV row with plausible values and no `SIM` flag.
6. Insert a microSD card: `STATUS?` shows `sd=1`; Export writes a CSV file.
7. RS-485: read input registers 0-27 with any Modbus master.

## 5. Loopback fixture (factory test)
On the board terminal: +12 V -> 2.2 k -> DO1 -> DI0, DI COM -> GND, +12 V -> DI1 (fixture
present). The `DIO_LOOP` test toggles DO1 20 times and checks DI0 follows (DO1 off -> DI0
active).

## 6. Event log
Every boot, reset reason, setting change, login, log repair, export and fault is a line in the
event log (docs/STORAGE.md section 6).

## 7. Self-test catalogue
| ID | Group | Checks |
|---|---|---|
| I2C_BUS | BOOT | lines idle, scan matches the expected device table |
| CH422G | BOOT | output write / read-back on spare pins |
| RTC | BOOT | oscillator ticks, time valid |
| TOUCH | BOOT | GT911 product ID |
| NVS | BOOT | NVS opens, usage, settings valid |
| STORAGE | BOOT | FAT mounted, log tail readable, free space |
| SENSOR | BOOT | one SHT reading with CRC check (WARN when simulated) |
| PSRAM, FLASH_ID | FULL | sizes and a PSRAM pattern test |
| SD | FULL | card present, free space (WARN: optional) |
| LOG_VERIFY | FULL | every log record decodes |
| DISPLAY, TOUCH_CORNERS | INTERACTIVE | operator confirms |
| DIO_LOOP | FIXTURE | DO1 -> DI0 loopback |
