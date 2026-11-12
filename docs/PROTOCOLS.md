# Protocols: USB serial, CSV, Modbus RTU, digital I/O

## 1. USB serial console
The USB-C port enumerates as a CDC-ACM serial port (any baud rate; the setting is ignored).
Lines end with CR, LF or CRLF, at most 159 characters. Commands and keywords are
case-insensitive. Every reply line ends with LF.

- Success: `OK` (or the requested data). Multi-line replies end with `END`.
- Failure: `ERR <code> <text>`.

| Code | Meaning |
|---|---|
| 1 | unknown command |
| 2 | syntax (unterminated quote, too many tokens) |
| 3 | wrong arguments |
| 4 | needs a higher access level (`LOGIN`) |
| 5 | busy (measurement or export running, queue full) |
| 6 | no data (e.g. no result yet) |
| 7 | failed (hardware, measurement fault) |
| 8 | not supported in this build / mode |
| 9 | line too long |
| 10 | login locked out (too many wrong codes) |

### 1.1 Commands
| Command | Level | Reply |
|---|---|---|
| `*IDN?` | operator | `envlogger,ENV-TRH-1,<serial>,<firmware>` |
| `MEAS` | operator | `OK`, then the result as one CSV row (or `ERR 7 ...` / `ERR 5 ...`) |
| `CANCEL` | operator | cancel a running measurement |
| `LAST?` | operator | the last result (CSV row) |
| `STREAM ON\|OFF` | operator | push every new result (manual and automatic) as a CSV row |
| `LOG? <n>` | operator | CSV header, the last n results oldest first, `END` |
| `LOG COUNT?` | operator | number of stored results |
| `AUTO?` | operator | `interval=<s> next_in=<s>` or `interval=0 OFF` |
| `CAL?` | operator | `temp_offset_c=<v> rh_offset_pct=<v>` |
| `TIME?` / `TIME <ISO-8601>` | operator / admin | clock in UTC, e.g. `TIME 2026-10-30T12:00:00Z` |
| `LOGIN ADMIN\|ENGINEER <code>` / `LOGOUT` / `WHOAMI?` | operator | access level of this serial session |
| `SET?` | operator | every setting: `key=... type=... range=... level=... unit=... default=... value=...` |
| `SET <key> <value>` | per setting | change a setting (e.g. `SET log_interval_s 600`) |
| `SELFTEST [LIST\|BOOT\|FULL\|FIXTURE\|<id>]` | operator | one `TEST <ID> <PASS\|WARN\|FAIL\|SKIP> <value> <unit> <detail>` line per test, then a summary |
| `IO?` / `WATCH <ms>` | operator | digital line states (repeat until any key) |
| `STATUS?` | operator | state snapshot |
| `I2C SCAN` | operator | ACKing addresses with names, then the I2C_BUS test |
| `IO SET <DO0\|DO1\|BL\|LCD_RST\|TP_RST> <0\|1>` | engineer | drive one line |
| `I2C READ <addr> <reg> <n>` / `I2C WRITE <addr> <reg> <bytes..>` | engineer | raw register access (hex) |
| `RTC?` | engineer | raw RTC time |
| `SENSOR RAW?` | engineer | sensor driver description and state |
| `SIM ON\|OFF` | engineer | simulated sensor (OFF only if a real sensor answered at boot) |
| `BOOT DIAG\|NORMAL` / `BOOTLOADER` | engineer | reboot into a boot mode / ROM download mode |
| `HELP` | operator | command list |

Factory defaults: Admin PIN `1234`, engineer code `1234`. Change both (`SET admin_pin ...`,
`SET engineer_code ...` as Engineer) before deployment.

## 2. CSV
One formatter (`el_csv`) for the serial stream, `LOG?`, the SD-card mirror and exports.
Columns:

```
timestamp,tag,operator,temperature_c,temperature_sd_c,humidity_pct,humidity_sd_pct,dew_point_c,
repeats,status,alarms,limit_t_low_c,limit_t_high_c,limit_rh_low_pct,limit_rh_high_pct,
device_serial,fw_version,record,flags
```

- `timestamp` ISO-8601 UTC. `tag` = the location setting at the time of the reading.
- Temperature / humidity use the `decimals` setting (0-2), SDs one decimal more.
- `status` NORMAL, ALARM or ERROR. `alarms` e.g. `T_HIGH|RH_LOW`.
- `record` is the log sequence number: unique per logger, use it to de-duplicate.
- `flags`: `SIM` (simulated sensor), `AUTO` (periodic logging), `ADJ` (calibration offset applied).
- Empty field = no value. Text is quoted per RFC 4180; text starting with `= + - @` gets a
  leading `'` (spreadsheet formula injection guard).

Example:
```
2026-10-30T12:00:00Z,Greenhouse 2,JD,24.6,0.02,65.2,0.15,17.6,3,NORMAL,,5.0,35.0,20.0,80.0,EL-0001,0.1.0,42,
```

## 3. Modbus RTU (RS-485)
Slave on the board's RS-485 terminal. Settings `mb_addr` (1-247), `mb_baud`
(9600-115200, default 19200), `mb_parity` (default EVEN; NONE uses 2 stop bits). Address,
baud and parity apply after a restart. Function codes: 03, 04, 06, 16, 08 (sub-function 0
echo) and 43/14 (device identification: vendor `envlogger`, product `ENV-TRH-1`, version).

32-bit values (float32, u32) use two registers, **high word first**. A request that starts or
ends inside a 32-bit value or string, or touches an unmapped register, gets exception 02.
Invalid values get exception 03; a START while busy gets exception 06.

### 3.1 Input registers (FC04)
| Reg | Type | Content |
|---|---|---|
| 0 | f32 | temperature of the last result, degC (NaN if none) |
| 2 | u16 | status: 0 none, 1 normal, 2 alarm, 3 error |
| 3 | u16 | result counter (wraps) |
| 4 | u32 | timestamp of the last result, Unix s |
| 6 | u16 | measurement state: 0 idle, 1 measuring, 2 done, 3 fault |
| 7 | u16 | alarm bits: 0 T low, 1 T high, 2 RH low, 3 RH high |
| 8 | u16 | progress % |
| 9 | u16 | flags: 0 sensor fault, 1 log nearly full, 2 export busy, 3 simulated, 4 automatic logging on, 5 SD card present |
| 10 | f32 | humidity, %RH |
| 12 | u16 | readings averaged |
| 13 | f32 | dew point, degC |
| 15 | f32 | temperature SD |
| 17 | u32 | log record number of the last result |
| 19 | f32 | humidity SD |
| 21 | u32 | uptime, s |
| 23 | u32 | results in the log |
| 25 | u16 | log capacity used, % |
| 26 | u32 | seconds to the next automatic reading (0xFFFFFFFF = off) |
| 30-32 | u16 | firmware major, minor, patch |
| 40 | 8 regs | serial number, ASCII (2 characters per register, first in the high byte) |
| 50 | 10 regs | tag of the last result, ASCII |

Registers 0-27 form one block and can be read in a single request.

### 3.2 Holding registers (FC03 / 06 / 16)
| Reg | Type | Content |
|---|---|---|
| 100 | u16 | command: 1 start a measurement, 2 cancel, 3 clear the DO1 alarm (reads 0) |
| 101 | u16 | readings per result, 1-10 |
| 102 | f32 | low temperature alarm, degC (-40 = off) |
| 104 | f32 | high temperature alarm, degC (125 = off) |
| 106 | f32 | low humidity alarm, %RH (0 = off) |
| 108 | f32 | high humidity alarm, %RH (100 = off) |
| 110 | u16 | slave address (after restart) |
| 111 | u16 | baud: 0 9600, 1 19200, 2 38400, 3 57600, 4 115200 (after restart) |
| 112 | u16 | parity: 0 none, 1 even, 2 odd (after restart) |
| 113 | u32 | logging interval, s: 0 = off or 10-86400 |

A write is atomic: if any register in it is invalid, nothing is applied. Low must stay below
high for each pair; writing both in one request is judged on the new pair. Writes become
normal settings changes (Admin level) and are logged.

## 4. Digital I/O (board terminal)
- **DI0**: with setting `di0_trigger` on, a rising edge (debounced, 2 x 50 ms) starts a
  measurement, like the MEASURE button. A DI0 held active at power-up does not trigger.
- **DI1**: informational (shown in Diagnostics, `IO?`). With the `CONFIG_EL_FIXTURE_DETECT`
  build option, DI1 active at power-up selects the factory self-test boot.
- **DO1** (setting `do1_mode`): `OFF`; `ON_ALARM` = on while the last result is an ALARM, off
  after a normal result or a clear request (UI, Modbus command 3); `BUSY` = on while a
  measurement runs.
- **DO0**: spare, off.
