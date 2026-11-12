#pragma once
/* CH422G IO expander on the Waveshare ESP32-S3-Touch-LCD-4.3B: command bytes, bit map, output
 * polarity and the output shadow registers. Pure logic; the I2C transfers live in
 * main/board/ch422g.c. Every rule here is from docs/HARDWARE.md section 2-3:
 *
 *  - Address-as-command protocol, one data byte per command (CH422 datasheet s6, Waveshare
 *    CH422G.h:115-138): 0x24 mode, 0x23 OC outputs, 0x38 IO outputs, 0x26 IO read.
 *  - Only mode 0x00 (IO port = inputs) and 0x01 (IO port = outputs, OC push-pull) are ever
 *    written: that avoids the disputed OD_EN/A_SCAN bit positions (section 2.2).
 *  - OC polarity is inverted by the opto/NPN stage: OC bit 0 = output transistor ON (sinking),
 *    bit 1 = OFF (section 3). Boot-safe OC = 0x03: DO0 OFF, DO1 OFF.
 *  - EXIO0 (DI0) and EXIO5 (DI1) are opto collector nets with 4.7 k pull-ups: the output shadow
 *    keeps them 0 ALWAYS (driving 1 into an active opto = ~30 mA contention, section 2.3).
 *  - DI raw bits are inverted: 0 = input active (opto LED lit), 1 = idle (section 3b).
 *
 * Not thread-safe; the caller (board service, under board_expander_lock) serialises. */
#include <stdbool.h>
#include <stdint.h>
#include "el_err.h"

/* 7-bit I2C addresses that act as commands. */
#define EL_CH422G_CMD_MODE    0x24u
#define EL_CH422G_CMD_OC      0x23u
#define EL_CH422G_CMD_IO_WR   0x38u
#define EL_CH422G_CMD_IO_RD   0x26u

#define EL_CH422G_MODE_INPUT  0x00u   /* IO_OE = 0: IO0-7 inputs (weak pull-ups), OC push-pull */
#define EL_CH422G_MODE_OUTPUT 0x01u   /* IO_OE = 1: IO0-7 outputs, OC push-pull */

/* After switching the port to input, wait at least this long before reading 0x26
 * (Espressif driver waits 2 ms; Waveshare 05_IO_Test comment says >= 1 ms). */
#define EL_CH422G_INPUT_SETTLE_MS 2u

/* EXIO bit map (Waveshare wiki pinout + schematic, HARDWARE.md 2.5). */
#define EL_EXIO_DI0      0u   /* input only: isolated DI0 (board terminal) */
#define EL_EXIO_TP_RST   1u   /* GT911 reset, active low */
#define EL_EXIO_DISP     2u   /* backlight enable (AP3032 CTRL) + panel DISP, 1 = on */
#define EL_EXIO_LCD_RST  3u   /* goes to panel pin 35 (NC on the Waveshare sheet): harmless */
#define EL_EXIO_SD_CS    4u   /* microSD chip select, active low */
#define EL_EXIO_DI1      5u   /* input only: isolated DI1 (board terminal) */
#define EL_EXIO_SPARE6   6u   /* no net: used by the CH422G self-test read-back */
#define EL_EXIO_SPARE7   7u   /* no net */

#define EL_CH422G_IO_DI_MASK   ((uint8_t)((1u << EL_EXIO_DI0) | (1u << EL_EXIO_DI1)))   /* 0x21 */
#define EL_CH422G_IO_SPARE_MASK ((uint8_t)((1u << EL_EXIO_SPARE6) | (1u << EL_EXIO_SPARE7)))

#define EL_OC_DO0  0u   /* open-collector output DO0 (board terminal), spare: kept OFF */
#define EL_OC_DO1  1u   /* open-collector output DO1 (board terminal): alarm output */
#define EL_CH422G_OC_MASK  0x0Fu
#define EL_CH422G_OC_BOOT  0x03u   /* DO0 OFF, DO1 OFF; OC2/OC3 have no net */

typedef struct {
    uint8_t io;   /* last value written (or to be written) to 0x38; DI bits always 0 */
    uint8_t oc;   /* last value written (or to be written) to 0x23; 0 bit = output ON */
} el_hw_ch422g_t;

/* Boot-safe shadow: OC = 0x03 (DO0/DO1 OFF); IO: backlight off, touch and LCD held in reset,
 * SD CS high (deselected), DI bits 0. */
void el_hw_ch422g_boot(el_hw_ch422g_t *s);

/* Set one output bit in the IO shadow. EL_ERR_ARG for bit > 7 or for the DI bits 0 and 5
 * (those can never be driven, whatever the level). */
el_err_t el_hw_ch422g_io_set(el_hw_ch422g_t *s, uint8_t bit, bool level);
bool     el_hw_ch422g_io_get(const el_hw_ch422g_t *s, uint8_t bit);

/* The byte to send to 0x38: the shadow with the DI bits forced to 0 (defence in depth). */
uint8_t  el_hw_ch422g_io_byte(const el_hw_ch422g_t *s);

/* OC outputs: on = transistor sinking. EL_ERR_ARG for oc_bit > 3. */
el_err_t el_hw_ch422g_oc_set(el_hw_ch422g_t *s, uint8_t oc_bit, bool on);
bool     el_hw_ch422g_oc_is_on(const el_hw_ch422g_t *s, uint8_t oc_bit);
uint8_t  el_hw_ch422g_oc_byte(const el_hw_ch422g_t *s);   /* masked to 4 bits */

/* Decode a raw 0x26 read taken with the port in INPUT mode: true = input active. */
void el_hw_ch422g_di_decode(uint8_t raw, bool *di0_active, bool *di1_active);

/* Self-test read-back: with the port in OUTPUT mode, 0x26 returns the output state
 * (CH422 datasheet s6.4). True if every non-DI bit of raw matches the shadow. */
bool el_hw_ch422g_readback_ok(const el_hw_ch422g_t *s, uint8_t raw);

/* Self-test pattern n (0..3): the shadow IO byte with the two unconnected bits (6, 7) set to
 * n's two bits; every connected output keeps its current level. */
uint8_t el_hw_ch422g_test_pattern(const el_hw_ch422g_t *s, unsigned n);

/* Engineer raw access names (board_io_set_raw): "DO0", "DO1", "BL", "LCD_RST", "TP_RST",
 * case-insensitive. DI0/DI1/EXIO5 and anything else -> EL_ERR_ARG.
 * For DO0/DO1 level true = output ON (sinking); for BL true = backlight on; for the resets
 * level is the pin level (false = held in reset). */
typedef enum { EL_HW_IO_OC = 0, EL_HW_IO_EXIO } el_hw_io_kind_t;
el_err_t el_hw_ch422g_parse_name(const char *name, el_hw_io_kind_t *kind, uint8_t *bit);
