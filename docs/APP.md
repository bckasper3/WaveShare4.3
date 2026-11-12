# App service

`main/app/`, `main/sensor/`, `main/comms/`, `main/console/`, `main/net/`, `main.c`. The logic
is in `envlogger_core` (`el_app_*`, `el_meas`, `el_autolog`); the files here wire it to tasks.

## 1. Boot and boot modes
`main.c` follows docs/FIRMWARE_ARCH.md section 3. The boot mode is decided by
`el_app_boot_decide()` (fixture -> FACTORY, bring-up build or one-shot flag -> DIAG, else
NORMAL); the one-shot flag lives in RTC no-init RAM with a magic and its complement, so random
RAM after a power-up is never read as a request. The boot self-test tally
(`el_app_tally_*`) decides whether a new OTA image may be confirmed.

## 2. Controller (`app_controller.c`)
- Owns `el_meas` and `el_autolog`; one task, event queue + 20 ms tick.
- Requests: `EVT_REQ_MEASURE` (UI, serial, Modbus, DI0), `EVT_REQ_CANCEL`,
  `EVT_REQ_EXPORT`, `EVT_REQ_CLEAR_ALARM`, `EVT_REQ_DISCARD_RESULT`, `EVT_REQ_FW_UPDATE`.
- The measurement environment comes from the settings (`el_app_meas_env`: repeats, limits,
  offsets) and the conditions (export busy, log full).
- The result's identity (timestamp, location tag, operator) is fixed when the request is
  accepted (`el_app_result_fill`). Automatic readings have no operator.
- Logging: the result is appended to the internal log; on failure it stays pending and is
  retried (`el_app_retry`: every 5 s, 3 times), then waits for the operator.
- Periodic logging: when the engine is idle and `el_autolog_due()`, a run starts with source
  AUTO; the slot is then marked taken whether the request was accepted or refused.
- Events out: `EVT_MEAS_STARTED` (arg = source), `PROGRESS`, `DONE`, `FAULT`, `CANCELLED`,
  `REJECTED`, `LOG_APPEND_FAILED`. Listeners (UI, console, Modbus, DO1) run in this task.

## 3. Shared state
`app_state.h`: one snapshot, each field with one writer, copied under a mutex by readers.

## 4. Modbus (`comms/modbus_slave.c`)
The UART task feeds frames (split by the RX timeout = t3.5, `el_app_mb_rx_tout_chars`) to the
`el_modbus` engine and transmits its answer. Every 250 ms the input registers are refreshed
from app_state (`el_app_mb_inputs`) and the holding registers from the settings
(`el_app_mb_holding`). Accepted writes are applied as Admin settings changes in a safe order
(`el_app_mb_plan`: for each low/high pair, `el_app_pair_high_first` decides which goes first
so every intermediate state passes the low < high rule). The register map: docs/PROTOCOLS.md
section 3.

## 5. Digital I/O (`comms/dio.c`)
Polls DI0/DI1 every 50 ms (1 s when the screen is off and DI0 triggering is disabled, because
each read switches the CH422G port), debounces them (`el_app_di`: 2 equal reads), posts a
measurement on a DI0 rising edge when `di0_trigger` is on, and drives DO1 from `el_app_do1`
(modes OFF / ON_ALARM / BUSY).

## 6. Serial console (`console/console.c`)
Bytes from the CDC port are split into lines on the USB task and executed one at a time by
the console task through the `el_cmd` table (level checks, error replies). Argument parsing:
`el_app_console`. Command list: docs/PROTOCOLS.md section 1.
