#include <string.h>
#include "el_store.h"
#include "el_util.h"

/* esp_image_header_t (24 B), esp_image_segment_header_t (8 B), then esp_app_desc_t. Offsets
 * from esp_app_format.h / esp_app_desc.h (ESP-IDF v5.5); frozen by Espressif for compatibility
 * with the ROM / 2nd-stage bootloader. */
enum {
    IMG_MAGIC = 0, IMG_SEGMENTS = 1, IMG_CHIP_ID = 12, IMG_HASH_APPENDED = 23,
    SEG0_LEN = 24 + 4,
    DESC = 32,
    DESC_MAGIC = DESC + 0, DESC_SECURE_VER = DESC + 4, DESC_VERSION = DESC + 16,
    DESC_PROJECT = DESC + 48, DESC_TIME = DESC + 80, DESC_DATE = DESC + 96,
    DESC_IDF_VER = DESC + 112,
    DESC_SIZE = 256,
};
_Static_assert(DESC + DESC_SIZE == EL_STORE_FW_PROBE_SIZE, "probe covers the descriptor");

static void get_text(char *dst, size_t cap, const uint8_t *src, size_t field)
{
    size_t n = 0;
    while (n < field && n + 1 < cap && src[n]) { dst[n] = (char)src[n]; n++; }
    dst[n] = '\0';
}

el_err_t el_store_fw_parse(const uint8_t *buf, size_t len, el_store_fw_info_t *out)
{
    if (!buf || !out) return EL_ERR_ARG;
    memset(out, 0, sizeof *out);
    if (len < EL_STORE_FW_PROBE_SIZE) return EL_ERR_SIZE;
    if (buf[IMG_MAGIC] != EL_STORE_FW_IMAGE_MAGIC) return EL_ERR_FORMAT;
    if (buf[IMG_SEGMENTS] == 0 || buf[IMG_SEGMENTS] > EL_STORE_FW_MAX_SEGMENTS) return EL_ERR_FORMAT;
    /* The descriptor is the first thing in the first (DROM) segment. */
    if (el_get_u32le(buf + SEG0_LEN) < DESC_SIZE) return EL_ERR_FORMAT;
    if (el_get_u32le(buf + DESC_MAGIC) != EL_STORE_FW_DESC_MAGIC) return EL_ERR_FORMAT;
    out->segments = buf[IMG_SEGMENTS];
    out->chip_id = el_get_u16le(buf + IMG_CHIP_ID);
    out->hash_appended = buf[IMG_HASH_APPENDED] == 1;
    out->secure_version = el_get_u32le(buf + DESC_SECURE_VER);
    get_text(out->version, sizeof out->version, buf + DESC_VERSION, 32);
    get_text(out->project, sizeof out->project, buf + DESC_PROJECT, 32);
    get_text(out->time, sizeof out->time, buf + DESC_TIME, 16);
    get_text(out->date, sizeof out->date, buf + DESC_DATE, 16);
    get_text(out->idf_ver, sizeof out->idf_ver, buf + DESC_IDF_VER, 32);
    return EL_OK;
}

typedef struct {
    uint32_t num[4];
    const char *suffix;     /* after '-', or NULL for a release */
} ver_t;

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

/* false if s does not start (after an optional v) with a digit. */
static bool parse_ver(const char *s, ver_t *v)
{
    memset(v, 0, sizeof *v);
    if (*s == 'v' || *s == 'V') s++;
    if (!is_digit(*s)) return false;
    for (int i = 0; i < 4; i++) {
        uint32_t x = 0;
        while (is_digit(*s)) {
            uint32_t d = (uint32_t)(*s - '0');
            x = x > (UINT32_MAX - d) / 10u ? UINT32_MAX : x * 10u + d;   /* saturate */
            s++;
        }
        v->num[i] = x;
        if (*s != '.' || !is_digit(s[1])) break;
        s++;
    }
    /* Anything after the numbers is a suffix; "-rc1" and "+build" alike (only '-' skipped). */
    if (*s) v->suffix = (*s == '-') ? s + 1 : s;
    return true;
}

static int sign(int x) { return (x > 0) - (x < 0); }

int el_store_version_cmp(const char *a, const char *b)
{
    if (!a || !b) return (a != NULL) - (b != NULL);
    ver_t va, vb;
    bool pa = parse_ver(a, &va), pb = parse_ver(b, &vb);
    if (!pa || !pb) return sign(strcmp(a, b));
    for (int i = 0; i < 4; i++)
        if (va.num[i] != vb.num[i]) return va.num[i] > vb.num[i] ? 1 : -1;
    if (!va.suffix && !vb.suffix) return 0;
    if (!va.suffix) return 1;               /* release above its pre-releases */
    if (!vb.suffix) return -1;
    return sign(strcmp(va.suffix, vb.suffix));
}

el_store_fw_verdict_t el_store_fw_check(const el_store_fw_info_t *img, uint32_t file_bytes,
                                        const el_store_fw_policy_t *pol)
{
    if (!img || !pol || !pol->project || !pol->running_version) return EL_STORE_FW_BAD_IMAGE;
    if (file_bytes < EL_STORE_FW_PROBE_SIZE || img->segments == 0) return EL_STORE_FW_BAD_IMAGE;
    if (img->chip_id != pol->chip_id) return EL_STORE_FW_WRONG_CHIP;
    if (strcmp(img->project, pol->project) != 0) return EL_STORE_FW_WRONG_PROJECT;
    if (file_bytes > pol->slot_bytes) return EL_STORE_FW_TOO_BIG;
    if (!pol->allow_downgrade && el_store_version_cmp(img->version, pol->running_version) < 0)
        return EL_STORE_FW_DOWNGRADE;
    return EL_STORE_FW_OK;
}

const char *el_store_fw_verdict_name(el_store_fw_verdict_t v)
{
    static const char *const names[EL_STORE_FW_VERDICT_COUNT] = {
        "OK", "BAD_IMAGE", "WRONG_CHIP", "WRONG_PROJECT", "TOO_BIG", "DOWNGRADE",
    };
    return (unsigned)v < EL_STORE_FW_VERDICT_COUNT ? names[v] : "?";
}
