#pragma once
/* On-flash result record: fixed 128 bytes, explicit little-endian layout, CRC-32 at the end.
 * Fixed size gives O(1) random access (record i at offset i * EL_RECORD_SIZE) for History paging.
 * Capacity: 50,000 records = 6.4 MB, fits the 8 MB "storage" partition.
 *
 * Layout v1 (offsets in bytes):
 *   0  u16 magic 0xE17A   2 u8 version   3 u8 status   4 u32 seq   8 i64 timestamp_s
 *  16  tag[20]  36 operator_id[8]
 *  44  f32 temp_c  48 f32 temp_sd_c  52 f32 rh_pct  56 f32 rh_sd_pct
 *  60  f32 limit t_low  64 f32 t_high  68 f32 rh_low  72 f32 rh_high
 *  76  u8 repeats  77 u8 flags  78 u8 alarms  79..123 reserved (zero)  124 u32 crc32 over 0..123
 *
 * Evolving the format: a new field goes into the reserved bytes and the version is bumped;
 * decoders accept every version <= EL_RECORD_VERSION, so old logs stay readable forever.
 * dew_point_c is not stored; decode recomputes it from temp_c and rh_pct. */
#include <stdint.h>
#include "el_err.h"
#include "el_result.h"

#define EL_RECORD_SIZE     128
#define EL_RECORD_MAGIC    0xE17Au
#define EL_RECORD_VERSION  1

el_err_t el_record_encode(const el_result_t *r, uint8_t out[EL_RECORD_SIZE]);

/* EL_ERR_FORMAT: not a record (bad magic, all 0xFF erased flash, all zeros).
 * EL_ERR_CRC:    record damaged, e.g. torn by a power cut mid-write.
 * EL_ERR_VERSION: written by newer firmware. */
el_err_t el_record_decode(const uint8_t in[EL_RECORD_SIZE], el_result_t *r);
