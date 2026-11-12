#pragma once
/* Storage-service logic that does not need ESP-IDF (docs/STORAGE.md): two-slot power-safe blob
 * selection, file naming, export range / watermark, SD-mirror catch-up bookkeeping, event-log
 * line format and rotation policy, firmware image header checks.
 * Pure C, no clock, no I/O, no malloc. Not thread-safe: the caller (storage service, under its
 * mutex) owns every struct and serialises calls. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_err.h"
#include "el_result.h"

/* ================= Two-slot blobs =================
 * Each persisted blob is stored twice (NVS keys "<name>_a" / "<name>_b"). Each slot holds the
 * blob wrapped as (little-endian):
 *   0 u16 magic 0x5A3C   2 u8 version (1)   3 u8 tag (which blob)   4 u32 generation
 *   8 u16 payload_len          10 u16 reserved 0   12 payload[len]         12+len u32 CRC-32
 *   of bytes 0..11+len.
 * Generation 0 is never written. A write always goes to the slot that is NOT in use, with a
 * generation one above the highest seen, then the NVS commit. Reading picks the newest slot
 * whose wrapper is intact AND whose payload the caller's codec accepts; a torn, erased or
 * rejected slot falls back to the other. So a power cut at any byte of a write leaves either
 * the old value or the new one, never a mix (host-tested at every byte). */
#define EL_STORE_SLOT_MAGIC     0x5A3Cu
#define EL_STORE_SLOT_VERSION   1
#define EL_STORE_SLOT_HDR       12u
#define EL_STORE_SLOT_OVERHEAD  (EL_STORE_SLOT_HDR + 4u)
#define EL_STORE_SLOT_PAYLOAD_MAX 0xFFFFu

/* Blob tags (stored in the wrapper so a key can never be read as another blob). Stable. */
enum {
    EL_STORE_TAG_SETTINGS = 1,
    EL_STORE_TAG_EXPORT_WM = 7,     /* u32 last exported seq */
    EL_STORE_TAG_MIRROR_WM = 8,     /* u32 last seq copied to the SD mirror */
    EL_STORE_TAG_UPLINK_WM = 9,     /* u32 last seq delivered over Wi-Fi */
};

/* EL_ERR_ARG (NULL out, gen 0, len > PAYLOAD_MAX, payload NULL with len > 0), EL_ERR_SIZE. */
el_err_t el_store_slot_encode(uint8_t tag, uint32_t gen, const void *payload, size_t len,
                              uint8_t *out, size_t cap, size_t *out_len);
/* EL_ERR_FORMAT (short, bad magic, wrong tag, gen 0, length mismatch), EL_ERR_VERSION (newer
 * wrapper), EL_ERR_CRC. Trailing bytes after the CRC are rejected (EL_ERR_FORMAT): a slot is
 * always read back at its exact length. *payload points into `in`. */
el_err_t el_store_slot_decode(const uint8_t *in, size_t len, uint8_t tag, uint32_t *gen,
                              const uint8_t **payload, size_t *payload_len);

/* true when generation a is newer than b (serial-number arithmetic, survives wrap). */
bool el_store_gen_newer(uint32_t a, uint32_t b);

/* Codec check on a payload whose wrapper is intact (e.g. el_settings_decode() == EL_OK).
 * Reject newer-version or out-of-range blobs here so the older slot is used. */
typedef bool (*el_store_accept_fn)(const uint8_t *payload, size_t len, void *ctx);

typedef struct {
    uint32_t gen[2];      /* generation of an intact wrapper in slot 0/1; 0 = empty/torn/unknown */
    int8_t   current;     /* slot whose payload is in use; -1 = none (defaults in use) */
} el_store_pair_t;

void el_store_pair_init(el_store_pair_t *p);    /* nothing known: current -1 */

/* Chooses the slot to use from the two stored images (a/b may be NULL with len 0 = key absent).
 * accept may be NULL (wrapper CRC only). On EL_OK *payload / *payload_len (both may be NULL)
 * point into a or b. Errors (p->current = -1, gens still recorded so the next write avoids the
 * newest copy): EL_ERR_ARG (p NULL); EL_ERR_FORMAT nothing stored in either slot (fresh device);
 * EL_ERR_CRC something stored but no slot is intact and accepted. */
el_err_t el_store_pair_select(el_store_pair_t *p, uint8_t tag,
                              const uint8_t *a, size_t alen, const uint8_t *b, size_t blen,
                              el_store_accept_fn accept, void *ctx,
                              const uint8_t **payload, size_t *payload_len);

/* Slot and generation for the next write: never the current slot; with no current slot, the
 * one holding the older (or no) generation. gen = highest seen + 1, never 0. */
void el_store_pair_next(const el_store_pair_t *p, uint8_t *slot, uint32_t *gen);

/* Record the outcome of writing `slot` with `gen`. ok: it becomes current. Failed: its content
 * is unknown (gen 0), current is unchanged, so the retry overwrites the same slot again. */
void el_store_pair_written(el_store_pair_t *p, uint8_t slot, uint32_t gen, bool ok);

/* ================= File names =================
 * Serial numbers are made FAT-safe: [A-Za-z0-9-] kept, anything else becomes '_', empty ->
 * "UNSET", at most 15 characters. Dates are UTC (same as the CSV timestamps). */
#define EL_STORE_NAME_MAX   64      /* longest name below (48 chars) + NUL, with margin */

/* EL_ERR_ARG (NULL dst / cap 0), EL_ERR_SIZE (truncated, still NUL-terminated). */
el_err_t el_store_safe_serial(char *dst, size_t cap, const char *serial);
/* "ENVLOG_<serial>_<YYYYMMDD-HHMMSS>.csv"; n > 0 adds "_<n>" before ".csv" (name taken). */
el_err_t el_store_export_name(char *dst, size_t cap, const char *serial, int64_t unix_s, unsigned n);
/* "ENVLOG_<serial>_<YYYYMMDD>.csv" - one SD mirror file per UTC day of the RESULT timestamp. */
el_err_t el_store_mirror_name(char *dst, size_t cap, const char *serial, int64_t unix_s);

/* ================= Export range and watermark =================
 * Values equal storage_export_scope_t. Exports are by seq: [from_seq, end_seq), end_seq = the
 * log's next_seq when the export starts (results appended during an export are not in it). */
typedef enum { EL_STORE_EXP_ALL = 0, EL_STORE_EXP_SINCE_LAST } el_store_exp_scope_t;

/* watermark = last seq exported by a completed ALL / SINCE_LAST export (0 = never). A watermark
 * >= next_seq cannot be genuine (seq never goes back) and is treated as 0. EL_ERR_ARG on a bad
 * scope / NULL. from == end means nothing to export. */
el_err_t el_store_export_range(el_store_exp_scope_t scope, uint32_t watermark, uint32_t first_seq,
                               uint32_t next_seq, uint32_t *from_seq, uint32_t *end_seq);
/* New watermark after a SUCCESSFUL export (file renamed into place): moves to end_seq - 1
 * (never backwards). */
uint32_t el_store_export_watermark(el_store_exp_scope_t scope, uint32_t old_wm, uint32_t end_seq);
/* 0..100, rounded down; total 0 -> 100. */
uint8_t el_store_pct(uint32_t done, uint32_t total);

/* ================= SD mirror bookkeeping =================
 * The mirror copies the internal log to the card by seq, so a normal append and a back-fill
 * after the card was missing are the same operation. done_seq is persisted lazily (at most
 * every save interval): after a power cut the last few lines may be written twice (the CSV
 * `record` column de-duplicates), never skipped. */
typedef struct {
    uint32_t done_seq;      /* last seq written to the card */
    uint32_t saved_seq;     /* value last persisted */
    uint32_t saved_ms;      /* when it was persisted (caller's monotonic clock) */
} el_store_mirror_t;

void el_store_mirror_init(el_store_mirror_t *m, uint32_t saved_seq, uint32_t now_ms);
/* Next batch to copy: seqs [*from, *from + *n), at most max_n, never below first_seq (older
 * ones were deleted). false (n = 0) when the mirror is up to date. A done_seq >= next_seq
 * (the log was replaced) restarts the mirror at first_seq. */
bool el_store_mirror_next(el_store_mirror_t *m, uint32_t first_seq, uint32_t next_seq,
                          uint32_t max_n, uint32_t *from, uint32_t *n);
void el_store_mirror_done(el_store_mirror_t *m, uint32_t seq);     /* seq is on the card */
bool el_store_mirror_behind(const el_store_mirror_t *m, uint32_t first_seq, uint32_t next_seq);
/* done_seq differs from the saved value and interval_ms passed since the last save. */
bool el_store_mirror_save_due(const el_store_mirror_t *m, uint32_t now_ms, uint32_t interval_ms);
void el_store_mirror_saved(el_store_mirror_t *m, uint32_t now_ms);

/* ================= Event / diagnostic log =================
 * One line:  "2026-10-30T12:00:00Z up=123.456 <message>\n"
 * The clock is shown as "----------T--:--:--Z" before it was ever set (unix_s below
 * EL_STORE_TIME_VALID_S). Control characters in the message become spaces; a message too long
 * for cap is cut (the line always ends in '\n'). */
#define EL_STORE_TIME_VALID_S   1704067200   /* 2024-01-01T00:00:00Z */
#define EL_STORE_EVLINE_MAX     200          /* buffer for one line incl. NUL */
#define EL_STORE_EVLINE_MIN_CAP 40           /* smallest cap that holds the prefix + "\n" */

/* Returns the line length (without NUL); 0 if dst NULL or cap < EL_STORE_EVLINE_MIN_CAP. */
size_t el_store_evline_format(char *dst, size_t cap, int64_t unix_s, uint32_t uptime_ms,
                              const char *msg);
/* Rotate (current file -> ".1" copy, start a new one) before a write of add bytes would make
 * the file exceed limit. A single line longer than limit goes into an empty file anyway. */
bool el_store_evlog_rotate_due(uint32_t cur_size, uint32_t add, uint32_t limit);

/* ================= Firmware image checks =================
 * ESP-IDF app image: esp_image_header_t (24 B) + first segment header (8 B) + esp_app_desc_t
 * (256 B) at offset 32. The bytes at the start of the file are enough to reject a wrong file
 * before erasing the OTA slot; esp_ota_end() then checks the checksum / SHA-256 (and the
 * signature when secure boot is enabled). */
#define EL_STORE_FW_PROBE_SIZE      288u
#define EL_STORE_FW_IMAGE_MAGIC     0xE9u
#define EL_STORE_FW_DESC_MAGIC      0xABCD5432u
#define EL_STORE_FW_MAX_SEGMENTS    16u
#define EL_STORE_FW_CHIP_ESP32S3    9u        /* esp_chip_id_t ESP_CHIP_ID_ESP32S3 */

typedef struct {
    uint8_t  segments;
    uint16_t chip_id;
    bool     hash_appended;
    uint32_t secure_version;
    char     version[33];
    char     project[33];
    char     time[17];
    char     date[17];
    char     idf_ver[33];
} el_store_fw_info_t;

/* EL_ERR_ARG (NULL), EL_ERR_SIZE (len < PROBE_SIZE), EL_ERR_FORMAT (not an app image: magic,
 * segment count, first segment too short for the descriptor, descriptor magic). Strings are
 * copied NUL-terminated even if the image's field is not. */
el_err_t el_store_fw_parse(const uint8_t *buf, size_t len, el_store_fw_info_t *out);

/* Version order: an optional leading 'v', up to four dot-separated numbers (missing = 0), then
 * an optional "-suffix". A release sorts above the same numbers with a suffix ("1.2.0" >
 * "1.2.0-rc1"); two suffixes compare as strings. Text that does not start with a digit after
 * the optional 'v' is compared as a plain string. Returns <0, 0, >0. NULL sorts lowest. */
int el_store_version_cmp(const char *a, const char *b);

typedef enum {
    EL_STORE_FW_OK = 0,
    EL_STORE_FW_BAD_IMAGE,       /* not an ESP-IDF app image */
    EL_STORE_FW_WRONG_CHIP,
    EL_STORE_FW_WRONG_PROJECT,   /* project name differs: another product's firmware */
    EL_STORE_FW_TOO_BIG,         /* larger than the OTA slot */
    EL_STORE_FW_DOWNGRADE,       /* lower version and downgrade not allowed */
    EL_STORE_FW_VERDICT_COUNT
} el_store_fw_verdict_t;

typedef struct {
    const char *project;         /* running image's project name */
    const char *running_version;
    uint16_t    chip_id;
    uint32_t    slot_bytes;      /* size of the OTA partition that will receive it */
    bool        allow_downgrade; /* ENGINEER only */
} el_store_fw_policy_t;

/* Checks in the order of the enum (first failure wins). NULL arguments -> BAD_IMAGE. */
el_store_fw_verdict_t el_store_fw_check(const el_store_fw_info_t *img, uint32_t file_bytes,
                                        const el_store_fw_policy_t *pol);
const char *el_store_fw_verdict_name(el_store_fw_verdict_t v);
