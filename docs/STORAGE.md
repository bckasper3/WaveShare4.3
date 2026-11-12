# Storage service

The only module that touches NVS, the internal FAT partition and the SD card
(docs/FIRMWARE_ARCH.md section 1). Public contract: `main/storage/storage.h`. Everything that
can be tested without hardware (two-slot blobs, file names, export ranges, mirror bookkeeping,
event-line format, firmware-image checks) is in `components/envlogger_core/include/el_store.h`.

## 1. Files
| Part | File |
|---|---|
| Init, settings, result log API, NVS rebuild, self-tests | `storage.c` |
| NVS two-slot blobs | `storage_nvs.c` |
| Internal FAT mount + result log I/O | `storage_fat.c` |
| SD presence, CSV mirror, event log, storage task | `storage_sd.c` |
| CSV export to the SD card | `storage_export.c` |
| Firmware update from the SD card | `main/fwupdate/` (skeleton; image checks ready in `el_store_fw`) |

## 2. Media
| Medium | Where | Holds | When broken |
|---|---|---|---|
| NVS | namespace `envlog` | settings, export / mirror / upload watermarks | run on defaults, report it, **never auto-erase** |
| Internal FAT | `/data` (partition `storage`) | result log `log.bin`, event log ring | reformat only if it cannot be mounted at all (logged) |
| SD card (optional) | `/sd` | CSV mirror, event log, export target, firmware file | the logger works fully without it |

## 3. Two-slot blobs
Each blob is stored under two NVS keys (`<name>_a` / `<name>_b`), each wrapped as
`0 u16 magic 0x5A3C | 2 u8 version | 3 u8 tag | 4 u32 generation | 8 u16 length | 10 u16 0 |
payload | u32 CRC-32`. A write always goes to the slot that is not in use, with a higher
generation, then commits. Reading picks the newest slot whose wrapper is intact and whose
payload the codec accepts. A power cut at any byte leaves the old or the new value, never a mix
(host-tested at every byte).

| Blob | Tag | Keys | Payload |
|---|---|---|---|
| Settings | 1 | `set_a` / `set_b` | `el_settings` TLV codec |
| Export watermark | 7 | `expwm_a` / `expwm_b` | u32 last exported seq |
| SD mirror watermark | 8 | `mirwm_a` / `mirwm_b` | u32 last seq on the card |
| Upload watermark | 9 | `upwm_a` / `upwm_b` | u32 last seq accepted by the server |

NVS that will not initialise is **not** erased automatically (that would wipe the settings
silently). The logger runs on defaults and reports it; an engineer can rebuild NVS from RAM
(`storage_nvs_rebuild`).

## 4. Result log
`el_datalog` on `/data/log.bin`, see docs/DATALOG.md. A file that is not a result log is kept as
`log_bad.bin` and a new log is started; a log written by newer firmware is left untouched and
logging is disabled (reported).

## 5. SD mirror
- One CSV file per UTC day of the **result** timestamp: `ENVLOG_<serial>_<YYYYMMDD>.csv`.
- The mirror copies the internal log by seq, so a normal append and a back-fill after the card
  was missing are the same operation. The position is saved at most every 30 s: after a power
  cut a few lines may be written twice (the `record` column de-duplicates), never skipped.

## 6. Event log
- SD present: `/sd/ENVLOG_EVENTS.log` + `.1.log`, 2 x 1 MB. Otherwise an internal 2 x 32 kB ring.
- One line per event: `2026-10-30T12:00:00Z up=123.456 <message>`.
- `storage_event_log()` never blocks: lines are queued to the storage task.
- Secret settings print as `****`: PINs and passwords never reach a file.

## 7. Export
- Target: the SD card. Scopes: **SINCE_LAST** (new since the last export) and **ALL**.
- File name: `ENVLOG_<serial>_<YYYYMMDD-HHMMSS>.csv`, `_<n>` added if the name is taken.
- Written as `ENVLOG_EXPORT.TMP`, renamed into place when complete. The watermark moves only
  after the rename, and never backwards.
- Measurements are refused while an export reads the log (`EXPORT_BUSY`).

## 8. Firmware image checks (`el_store_fw`)
Before an OTA slot is erased, the first 288 bytes of the file are checked in this order:
not an ESP-IDF app image -> wrong chip -> wrong project name -> larger than the slot ->
downgrade (refused unless allowed). `esp_ota_end()` then checks the SHA-256 (and the
signature with secure boot).
