#pragma once
/* PCF85063A real-time clock (Waveshare U3, I2C 0x51): register map and the BCD time block
 * codec. Pure logic; the I2C transfers live in main/rtc/rtc.c.
 * Source: NXP PCF85063A datasheet rev 7 (C:\esp\reference\datasheets\PCF85063A.pdf) and
 * docs/HARDWARE.md section 6.
 *
 * Time block = registers 0x04..0x0A read or written in ONE burst: the chip blocks the time
 * counters during an access, so a burst can never tear across a carry (datasheet 8.4).
 *   [0] Seconds  bit7 OS (oscillator stopped -> time invalid), bits 6-0 BCD 0-59
 *   [1] Minutes  BCD 0-59        [2] Hours BCD 0-23 (24 h mode, CTRL1 12_24 = 0)
 *   [3] Days     BCD 1-31        [4] Weekdays 0-6 (0 = Sunday)
 *   [5] Months   BCD 1-12        [6] Years BCD 0-99
 * Year base: 2000. The chip adds 29 Feb when the year register is divisible by 4 (including
 * 00), which is right for 2000-2099 only. (Waveshare's demo stores year - 1970; that breaks
 * the chip's leap-year logic and is not used here.) */
#include <stdbool.h>
#include <stdint.h>
#include "el_err.h"

#define EL_PCF85063_ADDR        0x51u
#define EL_PCF85063_REG_CTRL1   0x00u
#define EL_PCF85063_REG_CTRL2   0x01u
#define EL_PCF85063_REG_OFFSET  0x02u
#define EL_PCF85063_REG_RAM     0x03u
#define EL_PCF85063_REG_TIME    0x04u   /* first of the 7-byte time block */
#define EL_PCF85063_TIME_LEN    7u

#define EL_PCF85063_CTRL1_CAP_SEL 0x01u   /* 12.5 pF load (Waveshare demo, HARDWARE.md 6) */
#define EL_PCF85063_CTRL1_12_24   0x02u   /* 1 = 12 h mode: never set */
#define EL_PCF85063_CTRL1_STOP    0x20u   /* freezes the clock and resets the prescaler */
#define EL_PCF85063_CTRL1_INIT    EL_PCF85063_CTRL1_CAP_SEL   /* 0x01: 12.5 pF, 24 h, running */
#define EL_PCF85063_SOFT_RESET    0x58u   /* datasheet 8.2.1.3 (the demo's 0x11 is wrong) */
#define EL_PCF85063_SECONDS_OS    0x80u

#define EL_PCF85063_YEAR_MIN    2000
#define EL_PCF85063_YEAR_MAX    2099
#define EL_PCF85063_UNIX_MIN    946684800LL     /* 2000-01-01T00:00:00Z */
#define EL_PCF85063_UNIX_MAX    4102444799LL    /* 2099-12-31T23:59:59Z */

/* BCD byte -> binary. EL_ERR_FORMAT if either nibble is > 9. */
el_err_t el_hw_bcd_to_bin(uint8_t bcd, uint8_t *bin);
/* 0..99 -> BCD. Values > 99 are clamped to 99 (callers validate first). */
uint8_t  el_hw_bin_to_bcd(uint8_t bin);

/* Decode the 7-byte time block. *os_flag reports the oscillator-stop bit (time not
 * trustworthy). EL_ERR_FORMAT for a non-BCD digit, EL_ERR_RANGE for an impossible date
 * (31 Feb, month 0, hour 25 ...), EL_ERR_ARG for NULL pointers. On error *unix_s is untouched;
 * *os_flag is always written when the pointer is valid. */
el_err_t el_hw_pcf85063_decode(const uint8_t regs[EL_PCF85063_TIME_LEN], int64_t *unix_s, bool *os_flag);

/* Encode UTC seconds into the 7-byte block (OS bit cleared, weekday filled in).
 * EL_ERR_RANGE outside 2000-01-01..2099-12-31. */
el_err_t el_hw_pcf85063_encode(int64_t unix_s, uint8_t regs[EL_PCF85063_TIME_LEN]);

/* 0 = Sunday ... 6 = Saturday (the chip's weekday assignment, datasheet table 25). */
uint8_t el_hw_weekday(int64_t unix_s);
