#include <math.h>
#include <string.h>
#include "el_record.h"
#include "el_crc.h"
#include "el_stats.h"
#include "el_util.h"

/* Field offsets: frozen for every version (see el_record.h). Never move one; add new fields
 * in the reserved range and bump EL_RECORD_VERSION. */
enum {
    OFF_MAGIC = 0, OFF_VERSION = 2, OFF_STATUS = 3, OFF_SEQ = 4, OFF_TIME = 8,
    OFF_TAG = 16, OFF_OPERATOR = 36,
    OFF_TEMP = 44, OFF_TEMP_SD = 48, OFF_RH = 52, OFF_RH_SD = 56,
    OFF_T_LOW = 60, OFF_T_HIGH = 64, OFF_RH_LOW = 68, OFF_RH_HIGH = 72,
    OFF_REPEATS = 76, OFF_FLAGS = 77, OFF_ALARMS = 78, OFF_RESERVED = 79, OFF_CRC = 124,
};

_Static_assert(OFF_OPERATOR == OFF_TAG + EL_TAG_LEN, "layout");
_Static_assert(OFF_TEMP     == OFF_OPERATOR + EL_OPERATOR_LEN, "layout");
_Static_assert(OFF_RESERVED <= OFF_CRC, "layout");
_Static_assert(OFF_CRC + 4  == EL_RECORD_SIZE, "layout");

/* Fixed-width text: at most len-1 chars, NUL padded. */
static void put_str(uint8_t *dst, const char *src, size_t len)
{
    memset(dst, 0, len);
    size_t n = strnlen(src, len - 1);
    memcpy(dst, src, n);
}

static void get_str(char *dst, const uint8_t *src, size_t len)
{
    memcpy(dst, src, len);
    dst[len - 1] = '\0';
}

el_err_t el_record_encode(const el_result_t *r, uint8_t out[EL_RECORD_SIZE])
{
    if (!r || !out) return EL_ERR_ARG;
    if ((unsigned)r->status >= EL_RES_STATUS_COUNT) return EL_ERR_RANGE;
    memset(out, 0, EL_RECORD_SIZE);
    el_put_u16le(out + OFF_MAGIC, EL_RECORD_MAGIC);
    out[OFF_VERSION] = EL_RECORD_VERSION;
    out[OFF_STATUS]  = (uint8_t)r->status;
    el_put_u32le(out + OFF_SEQ, r->seq);
    el_put_u64le(out + OFF_TIME, (uint64_t)r->timestamp_s);
    put_str(out + OFF_TAG,      r->tag,         EL_TAG_LEN);
    put_str(out + OFF_OPERATOR, r->operator_id, EL_OPERATOR_LEN);
    el_put_f32le(out + OFF_TEMP,    r->temp_c);
    el_put_f32le(out + OFF_TEMP_SD, r->temp_sd_c);
    el_put_f32le(out + OFF_RH,      r->rh_pct);
    el_put_f32le(out + OFF_RH_SD,   r->rh_sd_pct);
    el_put_f32le(out + OFF_T_LOW,   r->limits.t_low_c);
    el_put_f32le(out + OFF_T_HIGH,  r->limits.t_high_c);
    el_put_f32le(out + OFF_RH_LOW,  r->limits.rh_low_pct);
    el_put_f32le(out + OFF_RH_HIGH, r->limits.rh_high_pct);
    out[OFF_REPEATS] = r->repeats;
    out[OFF_FLAGS]   = r->flags;
    out[OFF_ALARMS]  = r->alarms;
    el_put_u32le(out + OFF_CRC, el_crc32(out, OFF_CRC));
    return EL_OK;
}

el_err_t el_record_decode(const uint8_t in[EL_RECORD_SIZE], el_result_t *r)
{
    if (!in || !r) return EL_ERR_ARG;
    if (el_get_u16le(in + OFF_MAGIC) != EL_RECORD_MAGIC || in[OFF_VERSION] == 0) return EL_ERR_FORMAT;
    if (el_get_u32le(in + OFF_CRC) != el_crc32(in, OFF_CRC)) return EL_ERR_CRC;
    if (in[OFF_VERSION] > EL_RECORD_VERSION) return EL_ERR_VERSION;
    if (in[OFF_STATUS] >= EL_RES_STATUS_COUNT) return EL_ERR_RANGE;

    memset(r, 0, sizeof *r);
    r->status      = (el_result_status_t)in[OFF_STATUS];
    r->seq         = el_get_u32le(in + OFF_SEQ);
    r->timestamp_s = (int64_t)el_get_u64le(in + OFF_TIME);
    get_str(r->tag,         in + OFF_TAG,      EL_TAG_LEN);
    get_str(r->operator_id, in + OFF_OPERATOR, EL_OPERATOR_LEN);
    r->temp_c             = el_get_f32le(in + OFF_TEMP);
    r->temp_sd_c          = el_get_f32le(in + OFF_TEMP_SD);
    r->rh_pct             = el_get_f32le(in + OFF_RH);
    r->rh_sd_pct          = el_get_f32le(in + OFF_RH_SD);
    r->limits.t_low_c     = el_get_f32le(in + OFF_T_LOW);
    r->limits.t_high_c    = el_get_f32le(in + OFF_T_HIGH);
    r->limits.rh_low_pct  = el_get_f32le(in + OFF_RH_LOW);
    r->limits.rh_high_pct = el_get_f32le(in + OFF_RH_HIGH);
    r->repeats            = in[OFF_REPEATS];
    r->flags              = in[OFF_FLAGS];
    r->alarms             = in[OFF_ALARMS];
    r->dew_point_c        = el_dew_point_c(r->temp_c, r->rh_pct);
    return EL_OK;
}
