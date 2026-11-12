# Result log (`el_datalog`)

The internal, crash-safe log of every result: an append-only file of fixed 128-byte records
(`el_record`) behind a small header, on the internal FAT partition. The engine is pure C with
the file I/O passed in, so every power-cut scenario is host-tested (`test_datalog.c` cuts the
power at every byte of an append, a header update and a delete).

## Record (v1, 128 bytes, little-endian)
`0 u16 magic 0xE17A`, `2 u8 version`, `3 u8 status`, `4 u32 seq`, `8 i64 timestamp_s`,
`16 tag[20]`, `36 operator_id[8]`, `44 f32 temp_c`, `48 f32 temp_sd_c`, `52 f32 rh_pct`,
`56 f32 rh_sd_pct`, `60..75 4 x f32 limits` (T low, T high, RH low, RH high), `76 u8 repeats`,
`77 u8 flags`, `78 u8 alarms`, `79..123 reserved (0)`, `124 u32 CRC-32` over bytes 0..123.
The dew point is recomputed on decode. New fields go into the reserved bytes with a new
version; decoders accept every older version.

## Header
Two 64-byte header slots at the start of the file: `0 u32 magic "ENLG" (0x474C4E45)`,
`4 u8 version`, `6 u16 record size`, `8 u32 generation`, `12 u32 base seq`, `16 i64 created`,
CRC-32. Writes alternate between the slots; the newest valid one wins.

## What the medium must guarantee
- A write that is cut leaves every byte either old or new (no garbage outside the range written).
- `truncate` only shrinks.
- FAT with `CONFIG_WL_SECTOR_SIZE_512` + `CONFIG_WL_SECTOR_MODE_SAFE` gives this for small
  writes; confirm on hardware with a power-cut rig before relying on it.

## Recovery at open
1. Pick the newest valid header slot (or create one for an empty / new file).
2. Drop a partial record at the end (length not a multiple of 128).
3. If the last record does not decode and looks torn (written prefix of a valid record), drop it.
4. Finish an interrupted delete-all.
The report (`el_datalog_report_t`) says what was repaired; it is logged as an event at boot.

## Reading
- Record i lives at a fixed offset: O(1) random access for History pages.
- `get` / `iterate` / `verify_all`: damaged records are skipped and counted, never fatal.
- `find_first_after_seq` finds an export / mirror / upload position by arithmetic.

## Full log
Capacity follows the partition (about 50,000 records in the 8 MB storage partition). At 100 %
new appends fail with `EL_ERR_SIZE` and the UI asks the user to export and clear
(TODO: optional ring-overwrite of the oldest records). `fill_pct` and `near_full` (>= 90 %)
drive the status displays.
