#pragma once
/* Crash-safe result log (docs/DATALOG.md). One file: a 128-byte header block followed by
 * fixed 128-byte el_record records, so record i lives at offset 128 * (i + 1) (O(1) access for
 * History paging). The engine never touches files: all I/O goes through el_datalog_io_t.
 * Design, recovery rules and capacity: docs/DATALOG.md.
 *
 * Sequence numbers: record i has seq == base_seq + i (enforced: a stored record whose seq differs
 * reads as EL_ERR_FORMAT). seq starts at 1, is never reused, survives reboot and delete_all
 * (delete_all moves base_seq to next_seq). 0 is never a valid seq.
 *
 * Guarantee (given an I/O implementation that meets the contract below): a power cut at any
 * byte of any write leaves a file that el_datalog_open() repairs automatically; every record
 * for which el_datalog_append() returned EL_OK is still there with the same seq; no seq that
 * was ever returned is handed out again. A record whose append returned an error may or may
 * not be present after a reboot (it was never reported as saved).
 *
 * Not thread-safe: the caller serialises every call on one el_datalog_t (one owner task). */
#include <stdbool.h>
#include <stdint.h>
#include "el_err.h"
#include "el_record.h"
#include "el_result.h"

#define EL_DATALOG_HEADER_SIZE      128u
#define EL_DATALOG_VERSION          1
#define EL_DATALOG_NEAR_FULL_PCT    90u         /* warn the user from this fill level */
#define EL_DATALOG_BATCH_RECORDS    8u          /* records read per I/O call when iterating */
/* Largest max_records whose file size still fits a uint32_t offset. */
#define EL_DATALOG_MAX_RECORDS_LIMIT ((UINT32_MAX / EL_RECORD_SIZE) - 1u)

/* Storage backend. Every function returns EL_OK or a non-EL_OK code, which the engine returns
 * to its caller unchanged (el_err.h has no I/O code yet: see docs/DATALOG.md). The engine never
 * calls them concurrently. Contract an implementation MUST meet:
 *  size:     current file length in bytes.
 *  read:     exactly len bytes from offset (offset + len <= size), or an error. No side effects.
 *  append:   adds len bytes at the end; returns EL_OK only once they are durable (ESP32: fwrite,
 *            fflush, fsync). On error or power cut the file may have grown by any 0..len bytes,
 *            holding a prefix of buf possibly followed by fill (0x00/0xFF/anything).
 *  write_at: overwrites [offset, offset+len) inside the file (never extends it); durable on EL_OK.
 *            On error or power cut, bytes in that range may be old, new or fill.
 *            Used only on the two 64-byte header slots.
 *  truncate: shrinks the file to len (never called to grow it); durable on EL_OK. On error or
 *            power cut the length may end anywhere between old and new.
 *  All:      bytes OUTSIDE the range being written are never altered, even by a power cut.
 *            (FAT on SPI flash only meets this with a power-safe sector layer: see DATALOG.md.) */
typedef struct {
    void *ctx;
    el_err_t (*size)(void *ctx, uint32_t *out_bytes);
    el_err_t (*read)(void *ctx, uint32_t offset, void *buf, uint32_t len);
    el_err_t (*append)(void *ctx, const void *buf, uint32_t len);
    el_err_t (*write_at)(void *ctx, uint32_t offset, const void *buf, uint32_t len);
    el_err_t (*truncate)(void *ctx, uint32_t len);
} el_datalog_io_t;

/* What open() found and repaired; for the boot log / diagnostics screen. */
typedef struct {
    uint32_t records;              /* records in the log after recovery (damaged ones included) */
    uint32_t next_seq;             /* seq the next append will get */
    uint32_t bytes_truncated;      /* partial tail + torn record + leftovers of a cut delete_all */
    bool     created;              /* file was empty: a new log was started */
    bool     header_recreated;     /* header was missing/torn and no records: new header written */
    bool     header_repaired;      /* header slot damaged or stale: rewritten from the good copy
                                      or from the records' own seq numbers */
    bool     torn_record_removed;  /* last record was an interrupted append: removed */
    bool     last_record_damaged;  /* last record is corrupt but complete-looking: kept (its seq
                                      is never reused); reads of it return EL_ERR_CRC */
    bool     delete_completed;     /* a delete_all cut by power loss was finished */
} el_datalog_report_t;

typedef struct {
    el_datalog_io_t io;
    uint32_t max_records;
    uint32_t count;                /* records in the file, damaged ones included */
    uint32_t base_seq;             /* seq of record 0 */
    uint32_t slot_gen[2];          /* generation on flash of header slot 0/1; 0 = invalid/unknown */
    int64_t  created_unix_s;
    int64_t  cleared_unix_s;       /* last delete_all, 0 = never */
    bool     is_open;
    bool     header_uncertain;     /* a header write failed: re-read it before the next change */
    uint8_t  batch[EL_DATALOG_BATCH_RECORDS * EL_RECORD_SIZE];
} el_datalog_t;

typedef enum { EL_DATALOG_OLDEST_FIRST = 0, EL_DATALOG_NEWEST_FIRST } el_datalog_dir_t;

/* Iteration callback: index in the log and the decoded record. Return false to stop.
 * It may call el_datalog_get() and the accessors, but must not iterate, append or delete. */
typedef bool (*el_datalog_cb_t)(uint32_t index, const el_result_t *r, void *ctx);

typedef struct {
    uint32_t examined;             /* indices looked at; resume at start +/- examined */
    uint32_t matched;              /* records passed to the callback */
    uint32_t bad;                  /* damaged records skipped (CRC, format, version, seq) */
    uint32_t first_bad_index;      /* UINT32_MAX if none */
    bool     stopped;              /* callback returned false */
} el_datalog_iter_t;

typedef struct {
    uint32_t good;
    uint32_t bad;
    uint32_t first_bad_index;      /* UINT32_MAX if none */
} el_datalog_verify_t;

/* Opens (or creates) the log and repairs it after a power cut. Reads a constant number of
 * records (header, last, first), never the whole file. io is copied; io->ctx must outlive log.
 * max_records: 1..EL_DATALOG_MAX_RECORDS_LIMIT. now_unix_s: stamped into a new header.
 * report may be NULL. Errors: EL_ERR_ARG; I/O errors; EL_ERR_VERSION (header written by newer
 * firmware: file left untouched); EL_ERR_FORMAT (no valid header and no readable record: not
 * our file, left untouched; the caller may rename it and retry). On error log is not open. */
el_err_t el_datalog_open(el_datalog_t *log, const el_datalog_io_t *io, uint32_t max_records,
                         int64_t now_unix_s, el_datalog_report_t *report);

/* Appends r with seq = next_seq (r->seq is ignored); *out_seq (may be NULL) gets it.
 * EL_OK means durable. Errors leave the in-RAM log unchanged: EL_ERR_SIZE log full,
 * EL_ERR_RANGE bad status / seq space exhausted, EL_ERR_FORMAT file shorter than the log
 * (changed behind our back: re-open), EL_ERR_ARG, or the I/O error. */
el_err_t el_datalog_append(el_datalog_t *log, const el_result_t *r, uint32_t *out_seq);

/* index 0 = oldest. EL_ERR_ARG index >= count; EL_ERR_CRC / FORMAT / VERSION / RANGE for a
 * damaged record (the rest of the log is unaffected); or the I/O error. */
el_err_t el_datalog_get(el_datalog_t *log, uint32_t index, el_result_t *out);

/* Visits at most `limit` indices starting at `start` (inclusive), towards newer or older
 * records, stopping at either end of the log. Damaged records are skipped and counted.
 * start >= count visits nothing and returns EL_OK. stats may be NULL.
 * Errors: EL_ERR_ARG, or the I/O error (stats tell how far it got). */
el_err_t el_datalog_iterate(el_datalog_t *log, uint32_t start, uint32_t limit,
                            el_datalog_dir_t dir, el_datalog_cb_t cb, void *ctx,
                            el_datalog_iter_t *stats);


/* Index of the first record with seq > `seq` ("export since last export"); count if none.
 * O(1) and no I/O, thanks to seq == base_seq + index. EL_ERR_ARG only. */
el_err_t el_datalog_find_first_after_seq(const el_datalog_t *log, uint32_t seq, uint32_t *out_index);

/* Empties the log; next_seq is kept. Crash-safe: after a cut the log is either the old one or
 * empty. EL_OK once the new header is durable (the file shrink is finished later if it fails).
 * Errors: EL_ERR_ARG, or the I/O error with the log unchanged. Access control is the caller's. */
el_err_t el_datalog_delete_all(el_datalog_t *log, int64_t now_unix_s);

/* Diagnostics: reads and checks every record (~6.4 MB at 50,000). EL_ERR_ARG or I/O error. */
el_err_t el_datalog_verify_all(el_datalog_t *log, el_datalog_verify_t *out);

/* Accessors (0 / false on a NULL or closed log). */
uint32_t el_datalog_count(const el_datalog_t *log);
uint32_t el_datalog_capacity(const el_datalog_t *log);
uint32_t el_datalog_next_seq(const el_datalog_t *log);
uint32_t el_datalog_first_seq(const el_datalog_t *log);     /* seq of index 0 (= next if empty) */
uint32_t el_datalog_fill_pct(const el_datalog_t *log);      /* 0..100, rounded down */
bool     el_datalog_near_full(const el_datalog_t *log);     /* fill >= EL_DATALOG_NEAR_FULL_PCT */
bool     el_datalog_full(const el_datalog_t *log);
