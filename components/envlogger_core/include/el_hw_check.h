#pragma once
/* Pure judgement logic for the board self-tests (main/board/board_selftests.c), kept here so
 * every threshold and decision is host-tested. Each function documents its source.
 * Pure functions, thread-safe. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "el_err.h"
#include "el_selftest.h"

/* ---- Memory pattern test (PSRAM) ------------------------------------------------------- */
/* Pass p (0..EL_HW_MEM_PASSES-1) defines the word expected at index i:
 *   0: address-in-address (i * 0x9E3779B1 + seed) - catches aliased / stuck address lines
 *   1: bitwise inverse of pass 0 - every cell holds both 0 and 1
 *   2: walking ones (1 << (i % 32)) - catches stuck/bridged data lines */
#define EL_HW_MEM_PASSES 3u
uint32_t el_hw_mem_word(size_t i, unsigned pass, uint32_t seed);
void     el_hw_mem_fill(uint32_t *buf, size_t words, unsigned pass, uint32_t seed);
/* Returns the number of mismatching words; *first_bad = index of the first (or words). */
size_t   el_hw_mem_check(const uint32_t *buf, size_t words, unsigned pass, uint32_t seed, size_t *first_bad);

/* ---- RTC tick ---------------------------------------------------------------------------- */
/* Two readings taken 1100 ms apart (wall clock) must differ by 1 or 2 s. 0 = oscillator not
 * running (FAIL); anything else = garbage (FAIL). */
#define EL_HW_RTC_TICK_WAIT_MS 1100u
el_test_outcome_t el_hw_judge_rtc_tick(int64_t t0_s, int64_t t1_s);

/* ---- DIO_LOOP ----------------------------------------------------------------------------- */
/* Loopback fixture on the board terminal (docs/DIAGNOSTICS.md): +12 V -> 2.2 k -> DO1 -> DI0,
 * DI COM -> GND, +12 V -> DI1 (= fixture present). DO1 OFF: DI0 sits at 12 V through 2.2 k,
 * LED current flows -> DI0 ACTIVE. DO1 ON: DO1 sinks DI0 -> DI0 idle
 * (docs/HARDWARE.md: "DO1 OFF -> DI0 raw bit 0 (active)"). */
#define EL_HW_DIO_CYCLES      20u
#define EL_HW_DIO_SETTLE_MS   20u     /* opto + 100 nF input filter; TODO: measure on real hardware */
bool el_hw_dio_expected_di0(bool do1_on);

/* ---- FLASH_ID ----------------------------------------------------------------------------- */
#define EL_HW_FLASH_EXPECT_BYTES (16u * 1024u * 1024u)   /* ESP32-S3-WROOM-1-N16R8 */
el_test_outcome_t el_hw_judge_flash_size(uint32_t bytes);

/* ---- PSRAM size --------------------------------------------------------------------------- */
#define EL_HW_PSRAM_EXPECT_BYTES (8u * 1024u * 1024u)    /* N16R8: 8 MB octal PSRAM */
el_test_outcome_t el_hw_judge_psram_size(size_t bytes);

/* ---- GT911 product ID --------------------------------------------------------------------- */
/* Register 0x8140..0x8143 holds ASCII "911\0" (GT911 datasheet register map). */
el_test_outcome_t el_hw_judge_gt911_id(const uint8_t id[4]);
