#include <stddef.h>
#include "el_hw_pcf85063.h"
#include "el_time.h"

el_err_t el_hw_bcd_to_bin(uint8_t bcd, uint8_t *bin)
{
    uint8_t hi = (uint8_t)(bcd >> 4), lo = (uint8_t)(bcd & 0x0Fu);
    if (!bin) return EL_ERR_ARG;
    if (hi > 9 || lo > 9) return EL_ERR_FORMAT;
    *bin = (uint8_t)(hi * 10u + lo);
    return EL_OK;
}

uint8_t el_hw_bin_to_bcd(uint8_t bin)
{
    if (bin > 99) bin = 99;
    return (uint8_t)(((bin / 10u) << 4) | (bin % 10u));
}

uint8_t el_hw_weekday(int64_t unix_s)
{
    int64_t days = unix_s / 86400;
    if (unix_s % 86400 < 0) days--;          /* floor for times before 1970 */
    int64_t w = (days + 4) % 7;              /* 1970-01-01 was a Thursday */
    if (w < 0) w += 7;
    return (uint8_t)w;
}

el_err_t el_hw_pcf85063_decode(const uint8_t r[EL_PCF85063_TIME_LEN], int64_t *unix_s, bool *os_flag)
{
    if (!r || !unix_s || !os_flag) return EL_ERR_ARG;
    *os_flag = (r[0] & EL_PCF85063_SECONDS_OS) != 0;

    /* Mask the unused bits as the datasheet defines them; they read as 0 but are not
     * guaranteed. Weekday r[4] is not needed to compute the time and is ignored. */
    static const uint8_t mask[EL_PCF85063_TIME_LEN] = { 0x7F, 0x7F, 0x3F, 0x3F, 0x07, 0x1F, 0xFF };
    uint8_t v[EL_PCF85063_TIME_LEN];
    for (size_t i = 0; i < EL_PCF85063_TIME_LEN; i++) {
        if (i == 4) { v[i] = (uint8_t)(r[i] & mask[i]); continue; }
        el_err_t e = el_hw_bcd_to_bin((uint8_t)(r[i] & mask[i]), &v[i]);
        if (e != EL_OK) return e;
    }
    el_datetime_t dt = {
        .year = (int16_t)(EL_PCF85063_YEAR_MIN + v[6]),
        .month = v[5], .day = v[3], .hour = v[2], .minute = v[1], .second = v[0],
    };
    int64_t t;
    if (el_datetime_to_time(&dt, &t) != EL_OK) return EL_ERR_RANGE;
    *unix_s = t;
    return EL_OK;
}

el_err_t el_hw_pcf85063_encode(int64_t unix_s, uint8_t r[EL_PCF85063_TIME_LEN])
{
    if (!r) return EL_ERR_ARG;
    if (unix_s < EL_PCF85063_UNIX_MIN || unix_s > EL_PCF85063_UNIX_MAX) return EL_ERR_RANGE;
    el_datetime_t dt;
    el_time_to_datetime(unix_s, &dt);
    r[0] = el_hw_bin_to_bcd(dt.second);                 /* OS = 0: clears the flag */
    r[1] = el_hw_bin_to_bcd(dt.minute);
    r[2] = el_hw_bin_to_bcd(dt.hour);
    r[3] = el_hw_bin_to_bcd(dt.day);
    r[4] = el_hw_weekday(unix_s);
    r[5] = el_hw_bin_to_bcd(dt.month);
    r[6] = el_hw_bin_to_bcd((uint8_t)(dt.year - EL_PCF85063_YEAR_MIN));
    return EL_OK;
}
