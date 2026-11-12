# Measurement, periodic logging and calibration

## 1. One measurement
A measurement is `repeats` sensor readings (setting, 1-10, default 3). The result is the mean
temperature and humidity, their sample SDs, the dew point (Magnus formula, b = 17.62,
c = 243.12 degC), a status and alarm bits. Logic: `el_meas` (state machine), helpers in
`el_app_result`; the firmware task only forwards requests and calls `el_meas_step()` every 20 ms.

```
IDLE -> MEASURING (x repeats) -> DONE --(logged)--> IDLE
             any error -> FAULT --(shown / next request)--> IDLE
             CANCEL    -> IDLE (no result)
```

Requests are refused (in this order): invalid settings, busy, a previous result not logged yet,
an export reading the log, log full. Faults: sensor not acknowledging, sensor timeout, CRC
error, out of range, internal, our own watchdog (2 s per reading), bad sample, stuck sensor
(optional). A faulted run never leaves a result.

Each result is written to the internal log before it is shown as saved. If the append fails the
result stays pending (DONE) and is retried every 5 s, 3 times; after that the operator can
discard it. Nothing is dropped silently.

## 2. Sensor drivers
Interface `el_sensor.h`: `begin_conversion`, `poll`, `abort`, `info`, `caps`, all non-blocking
with the time passed in.
- `el_sensor_sht` - Sensirion SHT4x (command 0xFD, 10 ms) and SHT3x (single shot 0x2400, 16 ms).
  Each 6-byte reply carries two CRC-8 checksums (polynomial 0x31, init 0xFF); a mismatch is
  reported as `CRC`. Conversion: T = -45 + 175 x raw / 65535; RH = -6 + 125 x raw / 65535
  (SHT4x) or 100 x raw / 65535 (SHT3x), cropped to 0-100 %.
- `el_sensor_sim` - simulated climate with seeded noise and scripted faults (tests, PC
  simulator, a logger without a sensor). Its results carry the `SIM` flag.

## 3. Alarm limits
Four settings: `temp_low_c`, `temp_high_c` (-40..125 degC), `rh_low_pct`, `rh_high_pct`
(0..100 %RH); low must stay below high. A value equal to a limit is inside. A limit set to the
end of its range can never be crossed, so that is how a single limit is switched off. The
limits in force are stored with every result.

Status: NORMAL inside all limits, ALARM when any is crossed (bits `T_LOW`, `T_HIGH`, `RH_LOW`,
`RH_HIGH`), ERROR when no valid value exists.

## 4. Periodic logging
Setting `log_interval_s`: 0 = off, else 10 s to 24 h (default 300 s). Logic: `el_autolog`.
- The first automatic reading comes one interval after power-up or after the interval changes.
- Readings follow a fixed grid (no drift from measurement time).
- If the logger is busy when a slot is due (a manual reading, an export), the slot is taken as
  soon as the engine is idle; slots missed entirely are skipped, not caught up.
- A refused slot (log full, export running) is logged as an event and skipped.
- Automatic readings run in the background: the screen does not change, but an ALARM result
  wakes the display and shows a toast. They carry the `AUTO` flag and no operator ID.

## 5. Calibration (offsets)
Two Admin settings are added to every reading: `temp_offset_c` (+/-10 degC) and
`rh_offset_pct` (+/-20 %RH; the result is clamped to 0-100 %). Results taken with a non-zero
offset carry the `ADJ` flag.

The Calibration screen sets them from a reference instrument: take a reading next to the
reference, enter what the reference shows, and the screen computes
`offset = reference - (reading - current offset)`. Offsets can also be set directly in
Settings, over USB (`SET temp_offset_c -0.3`) or reset to zero.
