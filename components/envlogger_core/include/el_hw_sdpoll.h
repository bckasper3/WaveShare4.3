#pragma once
/* SD card presence tracking for sd_card_poll(). The Waveshare slot has no card-detect line
 * (socket pin 9 CD is tied to GND on the schematic), so presence is inferred:
 *   - mounted:   each poll sends CMD13 (SEND_STATUS); EL_HW_SD_FAILS_TO_DROP consecutive
 *                failures = card removed -> unmount.
 *   - unmounted: try to mount, but after a failed attempt wait EL_HW_SD_RETRY_MS before the
 *                next one (a probe with no card costs a few hundred ms of SPI timeouts).
 * The caller passes now_ms; nothing here reads a clock. Not thread-safe; caller serialises. */
#include <stdbool.h>
#include <stdint.h>

#define EL_HW_SD_FAILS_TO_DROP 2u
#define EL_HW_SD_RETRY_MS      5000u

typedef enum { EL_HW_SD_ACT_NONE = 0, EL_HW_SD_ACT_STATUS, EL_HW_SD_ACT_MOUNT } el_hw_sd_action_t;

typedef struct {
    bool     mounted;
    uint8_t  fails;          /* consecutive CMD13 failures while mounted */
    bool     retry_armed;    /* a mount attempt failed; wait until retry_at_ms */
    uint32_t retry_at_ms;
} el_hw_sd_poll_t;

void el_hw_sd_poll_init(el_hw_sd_poll_t *p);

/* What to do now. */
el_hw_sd_action_t el_hw_sd_poll_next(const el_hw_sd_poll_t *p, uint32_t now_ms);

/* Report the outcome of the action. Returns true if the mounted state changed. For
 * EL_HW_SD_ACT_STATUS a return of true means "unmount now" (the caller does it). */
bool el_hw_sd_poll_report(el_hw_sd_poll_t *p, el_hw_sd_action_t act, bool ok, uint32_t now_ms);

/* Explicit mount/unmount by the storage service (sd_card_mount / sd_card_unmount). */
void el_hw_sd_poll_set_mounted(el_hw_sd_poll_t *p, bool mounted, uint32_t now_ms);
